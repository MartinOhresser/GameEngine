#include "ge/app/time.hpp"
#include "ge/ecs/commands.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

namespace ge::mod::transform2d {

void integrate(ecs::World& world) {
    const float step =
        world.resource<app::Time>()->fixed_delta * world.resource<Params>()->speed_scale;
    world.each<Position, const Velocity>(
        [step](ecs::Entity, Position& position, const Velocity& velocity) {
            position.x += velocity.x * step;
            position.y += velocity.y * step;
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
