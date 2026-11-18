#include "ge/mod/altitude/altitude.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <cmath>

namespace ge::mod::altitude {

void measure(ecs::World& world) {
    world.each<Altitude, const transform2d::Position>([](ecs::Entity, Altitude& altitude,
                                                         const transform2d::Position& position) {
        if (!altitude.started) {
            altitude.started = true;
            altitude.origin_y = position.y;
        }
        altitude.height = std::max(altitude.height, altitude.origin_y - position.y);
        altitude.score = static_cast<std::int32_t>(std::floor(altitude.height * altitude.scale));
    });
}

} // namespace ge::mod::altitude
