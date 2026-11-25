#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/app/time.hpp"
#include "ge/ecs/commands.hpp"
#include "ge/mod/loadout/loadout.gen.hpp"
#include "ge/mod/reserve/stock.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string_view>
#include <vector>

namespace ge::mod::loadout {

namespace {

constexpr std::uint32_t none = 0xFFFFFFFFU;

struct Input {
    const app::Actions* actions = nullptr;
    const app::ActionNames* names = nullptr;

    [[nodiscard]] std::string_view name(std::uint32_t action, std::string_view fallback) const {
        if (action != none && names != nullptr && action < names->names.size()) {
            return names->names[action];
        }
        return fallback;
    }
    [[nodiscard]] bool down(std::uint32_t action, std::string_view fallback = {}) const {
        const std::string_view named = name(action, fallback);
        return !named.empty() && actions->down(named);
    }
    [[nodiscard]] bool pressed(std::uint32_t action) const {
        const std::string_view named = name(action, {});
        return !named.empty() && actions->pressed(named);
    }
    [[nodiscard]] float value(std::uint32_t action, std::string_view fallback) const {
        return actions->value(name(action, fallback));
    }
};

bool in(std::uint32_t set, std::uint32_t item) { return item < 32U && ((set >> item) & 1U) != 0U; }

/// The powers a hand may hold, in the order of the list.
std::vector<std::uint32_t> choices(const Kit& kit) {
    std::vector<std::uint32_t> items;
    for (std::uint32_t item = 0; item < 32U; item += 1) {
        if (in(kit.unlocked | kit.offered, item)) {
            items.push_back(item);
        }
    }
    return items;
}

/// The hands, by slot (then by age).
std::vector<std::pair<ecs::Entity, Hand*>> hands_of(ecs::World& world) {
    std::vector<std::pair<ecs::Entity, Hand*>> hands;
    world.each<Hand>([&hands](ecs::Entity entity, Hand& hand) { hands.emplace_back(entity, &hand); });
    std::ranges::stable_sort(hands, [](const auto& a, const auto& b) {
        return a.second->slot < b.second->slot;
    });
    return hands;
}

/// Gives a hand an item, swapping with whichever other hand held it.
void give(std::vector<std::pair<ecs::Entity, Hand*>>& hands, Hand& hand, std::uint32_t item) {
    for (auto& [entity, other] : hands) {
        if (other != &hand && other->item == item) {
            other->item = hand.item;
        }
    }
    hand.item = item;
}

std::uint32_t next_item(const Kit& kit, std::uint32_t current) {
    const std::vector<std::uint32_t> items = choices(kit);
    if (items.empty()) {
        return current;
    }
    const auto after = std::ranges::upper_bound(items, current == none ? 0U : current);
    if (current == none) {
        return items.front();
    }
    return after == items.end() ? items.front() : *after;
}

/// The power under the pointer on the wheel, or none near its middle.
std::uint32_t wheel_pick(const Kit& kit, float px, float py) {
    const float dx = px - kit.wheel_x;
    const float dy = py - kit.wheel_y;
    const std::vector<std::uint32_t> items = choices(kit);
    if (items.empty() || std::sqrt(dx * dx + dy * dy) < kit.wheel_dead) {
        return none;
    }
    // 0 at the top, growing clockwise (y goes down on the screen).
    float turn = std::atan2(dx, -dy) / (2.0F * std::numbers::pi_v<float>);
    if (turn < 0.0F) {
        turn += 1.0F;
    }
    const float count = static_cast<float>(items.size());
    const auto index = static_cast<std::size_t>(std::floor(turn * count + 0.5F)) % items.size();
    return items[index];
}

void update_wheel(ecs::World& world, Kit& kit, const Input& input, float tick, bool changed) {
    auto* params = world.resource<transform2d::Params>();
    const bool held = input.down(kit.change, "change");
    kit.change_time = held ? kit.change_time + tick : 0.0F;
    if (changed) {
        kit.change_time = -1.0e6F; // a power changed with a trigger: no wheel during this hold
    }
    const float px = input.value(kit.pointer_x, "pointer_x");
    const float py = input.value(kit.pointer_y, "pointer_y");
    if (held && !kit.wheel_open && kit.change_time >= kit.wheel_delay) {
        kit.wheel_open = true;
        kit.wheel_x = px;
        kit.wheel_y = py;
        kit.restore = params->speed_scale;
        params->speed_scale = kit.restore * kit.wheel_slow;
    } else if (!held && kit.wheel_open) {
        kit.wheel_open = false;
        params->speed_scale = kit.restore;
    }
    kit.wheel_choice = kit.wheel_open ? wheel_pick(kit, px, py) : none;
}

/// Changes of power: with change held, a trigger gives its hand the next power, or the one
/// chosen on the wheel. Returns whether a hand changed.
bool change_powers(Kit& kit, std::vector<std::pair<ecs::Entity, Hand*>>& hands,
                   const Input& input) {
    if (!input.down(kit.change, "change")) {
        return false;
    }
    bool changed = false;
    for (auto& [entity, hand] : hands) {
        if (!input.pressed(hand->trigger)) {
            continue;
        }
        const std::uint32_t item = kit.wheel_open ? kit.wheel_choice : next_item(kit, hand->item);
        if (item != none) {
            give(hands, *hand, item);
        }
        hand->waiting = true;
        changed = true;
    }
    return changed;
}

/// The power two items make together, or none.
std::uint32_t combined(ecs::World& world, const Kit& kit, std::uint32_t a, std::uint32_t b) {
    std::uint32_t result = none;
    world.each<const Combo>([&](ecs::Entity, const Combo& combo) {
        const bool pair = (combo.a == a && combo.b == b) || (combo.a == b && combo.b == a);
        if (result == none && pair && in(kit.unlocked, a) && in(kit.unlocked, b)) {
            result = combo.result;
        }
    });
    return result;
}

/// Follows the triggers of the hands: who acts, alone or joined. Returns the power the two first
/// hands make together, or none.
std::uint32_t use_hands(ecs::World& world, const Kit& kit,
                        std::vector<std::pair<ecs::Entity, Hand*>>& hands, const Input& input,
                        float tick) {
    for (auto& [entity, hand] : hands) {
        const bool down = input.down(hand->trigger);
        hand->since = input.pressed(hand->trigger) ? 0.0F : hand->since + tick;
        if (!down) {
            hand->waiting = false;
            hand->joined = false;
        }
        if (kit.wheel_open && down) {
            hand->waiting = true;
        }
    }
    if (hands.size() < 2U) {
        for (auto& [entity, hand] : hands) {
            hand->acting = input.down(hand->trigger) && !hand->waiting;
        }
        return none;
    }
    Hand& first = *hands[0].second;
    Hand& second = *hands[1].second;
    const std::uint32_t pair = combined(world, kit, first.item, second.item);
    const bool both = input.down(first.trigger) && input.down(second.trigger) && !first.waiting &&
                      !second.waiting;
    if (pair != none && both && !first.joined &&
        std::abs(first.since - second.since) <= kit.together) {
        first.joined = true;
        second.joined = true;
    }
    if (first.joined != second.joined || (first.joined && !both)) {
        // One hand let go of a joined use: the other waits to be let go too.
        for (Hand* hand : {&first, &second}) {
            hand->waiting = hand->waiting || hand->joined || input.down(hand->trigger);
            hand->joined = false;
        }
    }
    for (auto& [entity, hand] : hands) {
        const bool may_join = pair != none && hand->since < kit.together;
        hand->acting = input.down(hand->trigger) && !hand->waiting && (hand->joined || !may_join);
    }
    return first.joined ? pair : none;
}

bool wanted(const Kit& kit, const std::vector<std::pair<ecs::Entity, Hand*>>& hands,
            std::uint32_t joined, std::uint32_t power) {
    if (joined != none) {
        return joined == power;
    }
    return in(kit.unlocked, power) &&
           std::ranges::any_of(hands, [power](const auto& entry) {
               return entry.second->acting && !entry.second->joined && entry.second->item == power;
           });
}

void run_abilities(ecs::World& world, const Kit& kit,
                   const std::vector<std::pair<ecs::Entity, Hand*>>& hands, std::uint32_t joined,
                   float tick) {
    auto* commands = world.resource<ecs::Commands>();
    world.each<Ability>([&](ecs::Entity entity, Ability& ability) {
        const bool asked = wanted(kit, hands, joined, ability.power);
        if (!asked) {
            ability.stopped = false;
        }
        bool on = asked && !ability.stopped;
        if (on && !ability.active) {
            if (reserve::spend(world, ability.kind, ability.per_use)) {
                ability.uses += 1U;
                ability.time = 0.0F;
            } else {
                ability.refused += 1U;
                ability.stopped = true;
                on = false;
            }
        }
        if (on && ability.active && !reserve::spend(world, ability.kind, ability.per_second * tick)) {
            on = false;
            ability.stopped = true;
        }
        ability.time = on ? ability.time + (ability.active ? tick : 0.0F) : 0.0F;
        ability.active = on;
        const bool disabled = world.has<transform2d::Disabled>(entity);
        if (on && disabled) {
            commands->remove<transform2d::Disabled>(entity);
        } else if (!on && !disabled) {
            commands->insert(entity, transform2d::Disabled{});
        }
    });
}

/// A held Shortcut puts its powers in the two first hands and uses them together; let go, the
/// hands get back what they held. Returns whether a Shortcut is engaged, and the power it makes.
bool shortcut(ecs::World& world, const Kit& kit,
              std::vector<std::pair<ecs::Entity, Hand*>>& hands, const Input& input,
              std::uint32_t& joined) {
    if (hands.size() < 2U) {
        return false;
    }
    Hand& first = *hands[0].second;
    Hand& second = *hands[1].second;
    bool engaged = false;
    world.each<Shortcut>([&](ecs::Entity, Shortcut& cut) {
        const bool held = input.down(cut.action);
        if (held && !cut.engaged && !engaged) {
            cut.held_first = first.item;
            cut.held_second = second.item;
            first.item = cut.first;
            second.item = cut.second;
            cut.engaged = true;
        } else if (!held && cut.engaged) {
            first.item = cut.held_first;
            second.item = cut.held_second;
            cut.engaged = false;
            for (Hand* hand : {&first, &second}) {
                hand->waiting = input.down(hand->trigger);
                hand->joined = false;
                hand->acting = false;
            }
        }
        if (cut.engaged) {
            engaged = true;
            joined = combined(world, kit, cut.first, cut.second);
            for (Hand* hand : {&first, &second}) {
                hand->acting = true;
                hand->joined = true;
            }
        }
    });
    return engaged;
}

} // namespace

void select(ecs::World& world) {
    const auto* actions = world.resource<app::Actions>();
    if (actions == nullptr) {
        return;
    }
    const Input input{actions, world.resource<app::ActionNames>()};
    Kit* kit = nullptr;
    world.each<Kit>([&kit](ecs::Entity, Kit& found) {
        if (kit == nullptr) {
            kit = &found;
        }
    });
    if (kit == nullptr) {
        return;
    }
    // Input timings count real game time, not the slowed one of the wheel.
    const float real_tick = world.resource<app::Time>()->fixed_delta;
    std::vector<std::pair<ecs::Entity, Hand*>> hands = hands_of(world);
    std::uint32_t joined = none;
    if (!shortcut(world, *kit, hands, input, joined)) {
        const bool changed = change_powers(*kit, hands, input);
        update_wheel(world, *kit, input, real_tick, changed);
        joined = use_hands(world, *kit, hands, input, real_tick);
    }
    const float tick = real_tick * world.resource<transform2d::Params>()->speed_scale;
    run_abilities(world, *kit, hands, joined, tick);
    kit->held = 0U;
    for (const auto& [entity, hand] : hands) {
        if (hand->item < 32U) {
            kit->held |= 1U << hand->item;
        }
    }
}

} // namespace ge::mod::loadout
