#include "ge/app/time.hpp"
#include "ge/mod/gravity2d/gravity2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>

namespace ge::mod::gravity2d {

namespace {

/// `speed` after one step of `pull` along an axis: at most `terminal` in the
/// direction of the pull (a terminal of 0 or less means no limit).
float pulled(float speed, float pull, float step, float terminal) {
    speed += pull * step;
    if (terminal <= 0.0F) {
        return speed;
    }
    if (pull > 0.0F) {
        return std::min(speed, terminal);
    }
    return pull < 0.0F ? std::max(speed, -terminal) : speed;
}

} // namespace

void pull(ecs::World& world) {
    const float step = world.resource<app::Time>()->fixed_delta *
                       world.resource<transform2d::Params>()->speed_scale;
    world.each<const Gravity, transform2d::Velocity>(
        [step](ecs::Entity, const Gravity& gravity, transform2d::Velocity& velocity) {
            velocity.x = pulled(velocity.x, gravity.x, step, gravity.terminal_speed);
            velocity.y = pulled(velocity.y, gravity.y, step, gravity.terminal_speed);
        });
}

} // namespace ge::mod::gravity2d
