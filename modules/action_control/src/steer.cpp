#include "ge/app/input.hpp"
#include "ge/mod/action_control/action_control.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

namespace ge::mod::action_control {

void steer(ecs::World& world) {
    const auto* actions = world.resource<app::Actions>();
    if (actions == nullptr) {
        return;
    }
    const float x = actions->value("move_x");
    const float y = actions->value("move_y");
    world.each<const Controlled, transform2d::Velocity>(
        [x, y](ecs::Entity, const Controlled& controlled, transform2d::Velocity& velocity) {
            velocity.x = x * controlled.speed;
            velocity.y = y * controlled.speed;
        });
}

} // namespace ge::mod::action_control
