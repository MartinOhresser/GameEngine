#include "ge/app/fields.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/app/time.hpp"
#include "ge/mod/reserve/reserve.gen.hpp"
#include "ge/mod/reserve/stock.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace ge::mod::reserve {

namespace {

/// The number a field of the game names, on this entity; nothing when it has no such field.
std::optional<float> number_of(const ecs::World& world, ecs::Entity entity,
                               const app::FieldTable* table, std::uint32_t field) {
    if (table == nullptr || field >= table->entries.size()) {
        return std::nullopt;
    }
    const app::FieldEntry& entry = table->entries[field];
    if (entry.component == nullptr || entry.component->read == nullptr) {
        return std::nullopt;
    }
    const auto value = entry.component->read(world, entity, entry.field);
    return value ? std::optional<float>(app::scalar_as<float>(*value)) : std::nullopt;
}

void press(ecs::World& world, std::uint32_t action) {
    auto* actions = world.resource<app::Actions>();
    const auto* names = world.resource<app::ActionNames>();
    if (actions != nullptr && names != nullptr && action < names->names.size()) {
        actions->raise(names->names[action]);
    }
}

} // namespace

void flow(ecs::World& world) {
    const float tick = world.resource<app::Time>()->fixed_delta *
                       world.resource<transform2d::Params>()->speed_scale;
    const auto* table = world.resource<app::FieldTable>();
    world.each<const Gain>([&](ecs::Entity entity, const Gain& gain) {
        const std::optional<float> count = number_of(world, entity, table, gain.field);
        if (count) {
            give(world, gain.kind, std::abs(*count) * gain.per_unit);
        }
    });
    world.each<const Drain>([&](ecs::Entity entity, const Drain& use) {
        if (world.has<transform2d::Disabled>(entity)) {
            return;
        }
        const float units = std::abs(number_of(world, entity, table, use.field).value_or(0.0F));
        drain(world, use.kind, (use.per_second + units * use.per_unit) * tick);
    });
    world.each<Reserve>([&](ecs::Entity entity, Reserve& stock) {
        const bool had_some = stock.fill > 0.0F;
        if (!world.has<transform2d::Disabled>(entity)) {
            const float lowest = std::min(stock.floor, stock.amount);
            stock.amount = std::max(stock.amount - stock.drain * tick, lowest);
        }
        stock.amount = std::clamp(stock.amount, 0.0F, std::max(stock.max, 0.0F));
        stock.fill = stock.max > 0.0F ? stock.amount / stock.max : 0.0F;
        if (had_some && stock.amount <= 0.0F) {
            press(world, stock.empty_action);
        }
    });
}

} // namespace ge::mod::reserve
