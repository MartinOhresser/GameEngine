#include "ge/app/fields.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/ecs/commands.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"
#include "ge/mod/tween2d/tween2d.gen.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ge::mod::tween2d {

namespace {

/// The entity whose number is animated: the entity itself, or its owner.
ecs::Entity target_of(const ecs::World& world, ecs::Entity entity, bool on_owner) {
    if (!on_owner) {
        return entity;
    }
    const auto* owner = world.get<transform2d::Owner>(entity);
    return owner == nullptr ? ecs::Entity{} : ecs::Entity::from_link(owner->entity);
}

const app::FieldEntry* field_of(const app::FieldTable* table, std::uint32_t index) {
    if (table == nullptr || index >= table->entries.size()) {
        return nullptr;
    }
    const app::FieldEntry& entry = table->entries[index];
    return entry.component == nullptr ? nullptr : &entry;
}

std::optional<float> read_number(const ecs::World& world, ecs::Entity target,
                                 const app::FieldEntry& field) {
    if (field.component->read == nullptr || !world.is_alive(target)) {
        return std::nullopt;
    }
    const auto value = field.component->read(world, target, field.field);
    return value ? std::optional<float>(app::scalar_as<float>(*value)) : std::nullopt;
}

void write_number(ecs::World& world, ecs::Entity target, const app::FieldEntry& field,
                  float value) {
    if (field.component->write != nullptr && world.is_alive(target)) {
        static_cast<void>(
            field.component->write(world, target, field.field, app::Scalar{double{value}}));
    }
}

/// The curve: 0 to 1 in, 0 to 1 out (4 goes past 1 on the way).
float curve(std::uint32_t easing, float t) {
    switch (easing) {
    case 1:
        return t * t;
    case 2:
        return 1.0F - (1.0F - t) * (1.0F - t);
    case 3:
        return t * t * (3.0F - 2.0F * t);
    case 4: {
        constexpr float overshoot = 1.70158F;
        const float u = t - 1.0F;
        return 1.0F + u * u * ((overshoot + 1.0F) * u + overshoot);
    }
    default:
        return t;
    }
}

} // namespace

void run(ecs::World& world) {
    const auto* table = world.resource<app::FieldTable>();
    auto* commands = world.resource<ecs::Commands>();
    const auto* names = world.resource<app::ActionNames>();
    auto* actions = world.resource<app::Actions>();
    world.each<Tween>([&](ecs::Entity entity, Tween& tween) {
        if (tween.delay > 0U) {
            --tween.delay;
            return;
        }
        const ecs::Entity target = target_of(world, entity, tween.on_owner);
        const app::FieldEntry* field = field_of(table, tween.field);
        if (!tween.started) {
            tween.started = true;
            if (tween.from_current && field != nullptr) {
                tween.from = read_number(world, target, *field).value_or(tween.from);
            }
        }
        ++tween.elapsed;
        const float t =
            tween.ticks == 0U
                ? 1.0F
                : std::min(static_cast<float>(tween.elapsed) / static_cast<float>(tween.ticks),
                           1.0F);
        // The last tick lands exactly on the goal, whatever the curve does on the way.
        const float shaped = t >= 1.0F ? 1.0F : curve(tween.easing, t);
        if (field != nullptr) {
            write_number(world, target, *field, tween.from + (tween.to - tween.from) * shaped);
        }
        if (t < 1.0F) {
            return;
        }
        if (tween.use_action && names != nullptr && actions != nullptr &&
            tween.action < names->names.size()) {
            actions->raise(names->names[tween.action]);
        }
        if (tween.despawn_when_done) {
            commands->despawn(entity);
        } else {
            commands->remove<Tween>(entity);
        }
    });
}

void settle(ecs::World& world) {
    const auto* table = world.resource<app::FieldTable>();
    world.each<const Settle>([&](ecs::Entity entity, const Settle& settling) {
        const app::FieldEntry* field = field_of(table, settling.field);
        if (field == nullptr) {
            return;
        }
        const ecs::Entity target = target_of(world, entity, settling.on_owner);
        const std::optional<float> now = read_number(world, target, *field);
        if (!now) {
            return;
        }
        const float rate = std::clamp(settling.rate, 0.0F, 1.0F);
        const float left = settling.toward - *now;
        const float next = std::abs(left) <= 0.01F ? settling.toward : *now + left * rate;
        if (next != *now) {
            write_number(world, target, *field, next);
        }
    });
}

} // namespace ge::mod::tween2d
