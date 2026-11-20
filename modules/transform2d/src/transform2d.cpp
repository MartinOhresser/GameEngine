#include "ge/app/time.hpp"
#include "ge/ecs/commands.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

namespace ge::mod::transform2d {

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

} // namespace ge::mod::transform2d
