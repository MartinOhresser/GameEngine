#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/app/spawn.hpp"
#include "ge/ecs/commands.hpp"
#include "ge/mod/economy/economy.gen.hpp"
#include "ge/mod/grid2d/cells.hpp"
#include "ge/mod/grid2d/grid2d.gen.hpp"
#include "ge/mod/health/health.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ge::mod::economy {

namespace {

constexpr std::uint32_t no_prefab = 0xFFFFFFFFU;

/// How many ticks a Placer waits for the entity that replaces an upgraded one to exist.
constexpr std::uint32_t reselect_ticks = 6;

struct Output {
    app::Actions* actions = nullptr;
    const app::ActionNames* names = nullptr;

    void raise(std::uint32_t action) const {
        if (actions != nullptr && names != nullptr &&
            static_cast<std::size_t>(action) < names->names.size()) {
            actions->raise(names->names[action]);
        }
    }
    [[nodiscard]] bool pressed(std::uint32_t action) const {
        return actions != nullptr && names != nullptr &&
               static_cast<std::size_t>(action) < names->names.size() &&
               actions->pressed(names->names[action]);
    }
};

/// The first Wallet of the world.
Wallet* purse(ecs::World& world) {
    Wallet* found = nullptr;
    world.each<Wallet>([&found](ecs::Entity, Wallet& wallet) {
        if (found == nullptr) {
            found = &wallet;
        }
    });
    return found;
}

bool can_be_selected(const ecs::World& world, ecs::Entity entity) {
    return !entity.is_null() && (world.has<Upgradable>(entity) || world.has<Sellable>(entity));
}

/// Makes `entity` the selection of the placer and tells what can be done with it.
void select(ecs::World& world, Placer& placer, ecs::Entity entity, const Output& out) {
    placer.selected = entity.to_link();
    const auto* upgrade = world.get<Upgradable>(entity);
    const auto* sale = world.get<Sellable>(entity);
    placer.upgrade_price = upgrade == nullptr ? 0 : upgrade->price;
    placer.sell_value = sale == nullptr ? 0 : sale->refund;
    out.raise(upgrade == nullptr ? placer.final_action : placer.selected_action);
}

void let_go(Placer& placer, const Output& out) {
    if (placer.selected != 0U) {
        placer.selected = 0;
        placer.upgrade_price = 0;
        placer.sell_value = 0;
        out.raise(placer.deselected_action);
    }
}

/// The entity on a cell, if it is something to select.
ecs::Entity occupant(ecs::World& world, const grid2d::Layout& layout, std::int32_t col,
                     std::int32_t row) {
    if (!layout.contains(col, row)) {
        return ecs::Entity{};
    }
    return grid2d::occupancy(world, layout)[layout.index(col, row)];
}

void click(ecs::World& world, Placer& placer, const grid2d::Layout& layout,
           const grid2d::GridCursor& cursor, const Output& out) {
    const ecs::Entity there = occupant(world, layout, cursor.col, cursor.row);
    if (can_be_selected(world, there)) {
        select(world, placer, there, out);
        return;
    }
    Wallet* wallet = purse(world);
    if (there.is_null() && placer.prefab != no_prefab && wallet != nullptr) {
        if (wallet->amount < placer.price) {
            out.raise(placer.refused_action);
        } else if (auto* requests = world.resource<app::SpawnRequests>()) {
            wallet->amount -= placer.price;
            requests->spawn(placer.prefab, layout.x_of(cursor.col), layout.y_of(cursor.row));
            out.raise(placer.placed_action);
        }
    }
    let_go(placer, out);
}

/// Replaces the selected entity by its next version, or sells it.
void change(ecs::World& world, Placer& placer, const grid2d::Layout& layout, bool upgrade,
            const Output& out) {
    const ecs::Entity chosen = ecs::Entity::from_link(placer.selected);
    Wallet* wallet = purse(world);
    auto* at = world.get<grid2d::GridPos>(chosen);
    if (!world.is_alive(chosen) || wallet == nullptr || at == nullptr) {
        return;
    }
    auto* commands = world.resource<ecs::Commands>();
    const std::int32_t col = at->col;
    const std::int32_t row = at->row;
    if (upgrade) {
        const auto* next = world.get<Upgradable>(chosen);
        auto* requests = world.resource<app::SpawnRequests>();
        if (next == nullptr || requests == nullptr) {
            return;
        }
        if (wallet->amount < next->price) {
            out.raise(placer.refused_action);
            return;
        }
        wallet->amount -= next->price;
        requests->spawn(next->next_prefab, layout.x_of(col), layout.y_of(row));
        placer.reselect_col = col;
        placer.reselect_row = row;
        placer.reselect_in = reselect_ticks;
    } else if (const auto* sale = world.get<Sellable>(chosen)) {
        wallet->amount += sale->refund;
    } else {
        return;
    }
    commands->despawn(chosen);
    placer.selected = 0;
    placer.upgrade_price = 0;
    placer.sell_value = 0;
    if (!upgrade) {
        out.raise(placer.deselected_action);
    }
}

/// After an improvement: selects the new version as soon as it exists.
void reselect(ecs::World& world, Placer& placer, const grid2d::Layout& layout, const Output& out) {
    if (placer.reselect_in == 0U) {
        return;
    }
    --placer.reselect_in;
    const ecs::Entity there = occupant(world, layout, placer.reselect_col, placer.reselect_row);
    if (can_be_selected(world, there)) {
        placer.reselect_in = 0;
        select(world, placer, there, out);
    } else if (placer.reselect_in == 0U) {
        out.raise(placer.deselected_action);
    }
}

/// The placer's own entity follows the selection and hides when there is none.
void follow_selection(ecs::World& world, ecs::Entity entity, const Placer& placer) {
    auto* commands = world.resource<ecs::Commands>();
    const ecs::Entity chosen = ecs::Entity::from_link(placer.selected);
    const auto* at = world.get<transform2d::Position>(chosen);
    auto* mine = world.get<transform2d::Position>(entity);
    const bool shown = placer.selected != 0U && world.is_alive(chosen) && at != nullptr;
    if (shown && mine != nullptr) {
        mine->x = at->x;
        mine->y = at->y;
    }
    if (shown && world.has<transform2d::Hidden>(entity)) {
        commands->remove<transform2d::Hidden>(entity);
    } else if (!shown && !world.has<transform2d::Hidden>(entity)) {
        commands->insert(entity, transform2d::Hidden{});
    }
}

void handle(ecs::World& world, ecs::Entity entity, const Output& out) {
    auto* placer = world.get<Placer>(entity);
    const auto* cursor = world.get<grid2d::GridCursor>(entity);
    const auto layout = placer == nullptr ? std::nullopt : grid2d::layout_of(world, placer->grid);
    if (placer == nullptr || !layout || world.has<transform2d::Disabled>(entity)) {
        return;
    }
    if (placer->selected != 0U && !world.is_alive(ecs::Entity::from_link(placer->selected))) {
        let_go(*placer, out); // sold, or destroyed by something else
    }
    if (out.pressed(placer->upgrade_action)) {
        change(world, *placer, *layout, true, out);
        // No `reselect` in the tick of the change: the entity it replaces is still there.
    } else if (out.pressed(placer->sell_action)) {
        change(world, *placer, *layout, false, out);
    } else {
        if (cursor != nullptr && cursor->clicked) {
            click(world, *placer, *layout, *cursor, out);
        }
        reselect(world, *placer, *layout, out);
    }
    follow_selection(world, entity, *placer);
}

} // namespace

void pay_bounties(ecs::World& world) {
    Wallet* wallet = purse(world);
    if (wallet == nullptr) {
        return;
    }
    world.each<Bounty, const health::Dead>(
        [wallet](ecs::Entity, Bounty& bounty, const health::Dead&) {
            if (!bounty.paid) {
                bounty.paid = true;
                wallet->amount += bounty.amount;
            }
        });
}

void build(ecs::World& world) {
    const Output out{world.resource<app::Actions>(), world.resource<app::ActionNames>()};
    std::vector<ecs::Entity> placers;
    world.each<const Placer>(
        [&placers](ecs::Entity entity, const Placer&) { placers.push_back(entity); });
    for (const ecs::Entity entity : placers) {
        handle(world, entity, out);
    }
}

} // namespace ge::mod::economy
