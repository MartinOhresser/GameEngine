#include "ge/app/time.hpp"
#include "ge/app/viewport.hpp"
#include "ge/ecs/commands.hpp"
#include "ge/mod/transform2d/drawn.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <utility>

namespace ge::mod::transform2d {

void remember(ecs::World& world) {
    const auto* time = world.resource<app::Time>();
    if (time == nullptr || !time->interpolate) {
        return; // nothing to smooth: tests and scenarios draw the last tick as it is
    }
    auto* trail = world.resource<Trail>();
    if (trail == nullptr) {
        trail = &world.insert_resource(Trail{});
    }
    trail->snap_distance = world.resource<Params>()->snap_distance;
    if (trail->latest.taken && trail->latest.tick == time->tick) {
        return; // no tick ran since the last look
    }
    std::swap(trail->older, trail->latest);
    PositionsAt& now = trail->latest;
    now.tick = time->tick;
    now.taken = true;
    for (PositionsAt::Entry& entry : now.entries) {
        entry.present = false;
    }
    world.each<const Position>([&now](ecs::Entity entity, const Position& position) {
        if (entity.index() >= now.entries.size()) {
            now.entries.resize(static_cast<std::size_t>(entity.index()) + 1U);
        }
        now.entries[entity.index()] = {entity.generation(), true, position};
    });
}

void integrate(ecs::World& world) {
    const float step =
        world.resource<app::Time>()->fixed_delta * world.resource<Params>()->speed_scale;
    world.each<Position, const Velocity>(
        [&](ecs::Entity entity, Position& position, const Velocity& velocity) {
            const auto* scale = world.get<SpeedScale>(entity);
            const float own = scale == nullptr ? 1.0F : scale->factor;
            position.x += velocity.x * step * own;
            position.y += velocity.y * step * own;
        });
}

void wear_off(ecs::World& world) {
    auto& commands = *world.resource<ecs::Commands>();
    world.each<SpeedScale>([&commands](ecs::Entity entity, SpeedScale& scale) {
        if (scale.ticks == 0U) {
            return;
        }
        --scale.ticks;
        if (scale.ticks == 0U) {
            commands.remove<SpeedScale>(entity);
        }
    });
}

void attach(ecs::World& world) {
    auto& commands = *world.resource<ecs::Commands>();
    world.each<const Attach, const Owner>(
        [&](ecs::Entity entity, const Attach& attached, const Owner& owner) {
            const ecs::Entity target = ecs::Entity::from_link(owner.entity);
            if (!world.is_alive(target)) {
                if (owner.entity != 0U && attached.despawn_with_owner) {
                    commands.despawn(entity);
                }
                return;
            }
            const auto* at = world.get<Position>(target);
            auto* position = world.get<Position>(entity);
            if (at != nullptr && position != nullptr) {
                position->x = at->x + attached.offset_x;
                position->y = at->y + attached.offset_y;
            }
        });
}

void anchor(ecs::World& world) {
    const auto* view = world.resource<app::Viewport>();
    if (view == nullptr) {
        return;
    }
    const auto width = static_cast<float>(view->width);
    const auto height = static_cast<float>(view->height);
    world.each<const Anchor, Position>(
        [width, height](ecs::Entity, const Anchor& at, Position& position) {
            position.x = width * at.anchor_x + at.offset_x;
            position.y = height * at.anchor_y + at.offset_y;
        });
}

} // namespace ge::mod::transform2d
