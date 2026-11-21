#include "ge/app/direction.hpp"
#include "ge/mod/progress/progress.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <cmath>

namespace ge::mod::progress {

void measure(ecs::World& world) {
    world.each<Progress, const transform2d::Position>([](ecs::Entity, Progress& progress,
                                                         const transform2d::Position& position) {
        const app::Direction toward = app::Direction::of(progress.toward_x, progress.toward_y);
        const float coordinate = toward.vertical ? position.y : position.x;
        if (!progress.started) {
            progress.started = true;
            progress.origin = coordinate;
        }
        progress.distance =
            std::max(progress.distance, toward.sign * (coordinate - progress.origin));
        progress.score = static_cast<std::int32_t>(std::floor(progress.distance * progress.scale));
    });
}

} // namespace ge::mod::progress
