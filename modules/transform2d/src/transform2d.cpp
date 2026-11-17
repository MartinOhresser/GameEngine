#include "ge/app/time.hpp"
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

} // namespace ge::mod::transform2d
