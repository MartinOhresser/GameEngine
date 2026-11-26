#include "ge/app/time.hpp"
#include "ge/mod/gravity2d/gravity2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"
#include "ge/mod/transform2d/world_speed.hpp"

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
    const float step = world.resource<app::Time>()->fixed_delta * transform2d::world_speed(world);
    world.each<const Gravity, transform2d::Velocity>(
        [&world, step](ecs::Entity entity, const Gravity& gravity, transform2d::Velocity& velocity) {
            // An entity slowed on its own (transform2d.SpeedScale) falls as slowly as it moves.
            const auto* own = world.get<transform2d::SpeedScale>(entity);
            const float scaled = own == nullptr ? step : step * own->factor;
            velocity.x = pulled(velocity.x, gravity.x, scaled, gravity.terminal_speed);
            velocity.y = pulled(velocity.y, gravity.y, scaled, gravity.terminal_speed);
        });
}

} // namespace ge::mod::gravity2d
