#include "ge/mod/patrol2d/patrol2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <cmath>

namespace ge::mod::patrol2d {

namespace {

/// Puts `position` back inside [low, high] and turns `speed` away from the limit it went past.
void turn_axis(float& position, float& speed, float low, float high) {
    if (position > high) {
        position = high;
        speed = -std::fabs(speed);
    } else if (position < low) {
        position = low;
        speed = std::fabs(speed);
    }
}

} // namespace

void turn(ecs::World& world) {
    world.each<const Patrol, transform2d::Position, transform2d::Velocity>(
        [](ecs::Entity, const Patrol& patrol, transform2d::Position& position,
           transform2d::Velocity& velocity) {
            if (patrol.patrol_x) {
                turn_axis(position.x, velocity.x, patrol.min_x, patrol.max_x);
            }
            if (patrol.patrol_y) {
                turn_axis(position.y, velocity.y, patrol.min_y, patrol.max_y);
            }
        });
}

} // namespace ge::mod::patrol2d
