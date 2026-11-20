#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/app/time.hpp"
#include "ge/ecs/commands.hpp"
#include "ge/mod/health/damage.hpp"
#include "ge/mod/path2d/path2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"
#include "ge/mod/ui/ui.gen.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ge::mod::path2d {

namespace {

struct Point {
    std::uint32_t path = 0;
    std::uint32_t index = 0;
    float x = 0.0F;
    float y = 0.0F;
};

/// Every waypoint of the world, by path and then by index.
std::vector<Point> waypoints(ecs::World& world) {
    std::vector<Point> points;
    world.each<const Waypoint, const transform2d::Position>(
        [&points](ecs::Entity, const Waypoint& waypoint, const transform2d::Position& position) {
            points.push_back({waypoint.path, waypoint.index, position.x, position.y});
        });
    std::ranges::sort(points, [](const Point& a, const Point& b) {
        return a.path != b.path ? a.path < b.path : a.index < b.index;
    });
    return points;
}

/// The points of one path, in order, as a view into the sorted list.
std::vector<Point> path_of(const std::vector<Point>& points, std::uint32_t path) {
    std::vector<Point> own;
    for (const Point& point : points) {
        if (point.path == path) {
            own.push_back(point);
        }
    }
    return own;
}

/// How far a follower goes in this tick: its speed times the tick, the game's speed and its own.
float reach_of(const ecs::World& world, ecs::Entity entity, const PathFollower& follower) {
    const auto* scale = world.get<transform2d::SpeedScale>(entity);
    const float own = scale == nullptr ? 1.0F : scale->factor;
    return follower.speed * world.resource<app::Time>()->fixed_delta *
           world.resource<transform2d::Params>()->speed_scale * own;
}

/// Walks `reach` units along the path from the follower's position; true when it has gone
/// past the last point.
bool walk(const std::vector<Point>& route, float reach, PathFollower& follower,
          transform2d::Position& position) {
    while (reach > 0.0F && static_cast<std::size_t>(follower.index) < route.size()) {
        const Point& target = route[follower.index];
        const float dx = target.x - position.x;
        const float dy = target.y - position.y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        if (distance <= reach) {
            position.x = target.x;
            position.y = target.y;
            reach -= distance;
            follower.progress += distance;
            ++follower.index;
        } else {
            position.x += dx / distance * reach;
            position.y += dy / distance * reach;
            follower.progress += reach;
            reach = 0.0F;
        }
    }
    return static_cast<std::size_t>(follower.index) >= route.size();
}

ecs::Entity tagged(ecs::World& world, std::uint32_t tag) {
    ecs::Entity found;
    world.each<const ui::Tag>([&](ecs::Entity entity, const ui::Tag& item) {
        if (found.is_null() && item.tag == tag) {
            found = entity;
        }
    });
    return found;
}

} // namespace

void follow(ecs::World& world) {
    const std::vector<Point> points = waypoints(world);
    if (points.empty()) {
        return;
    }
    auto* actions = world.resource<app::Actions>();
    const auto* names = world.resource<app::ActionNames>();
    std::vector<ecs::Entity> arrived;
    world.each<PathFollower, transform2d::Position>([&](ecs::Entity entity, PathFollower& follower,
                                                        transform2d::Position& position) {
        if (world.has<transform2d::Disabled>(entity)) {
            return;
        }
        const std::vector<Point> route = path_of(points, follower.path);
        if (!route.empty() && walk(route, reach_of(world, entity, follower), follower, position)) {
            arrived.push_back(entity);
        }
    });
    for (const ecs::Entity entity : arrived) {
        const auto* follower = world.get<PathFollower>(entity);
        const ecs::Entity target = tagged(world, follower->end_target);
        if (!target.is_null()) {
            health::deal(world, target, follower->end_damage);
        }
        if (actions != nullptr && names != nullptr &&
            static_cast<std::size_t>(follower->reached_action) < names->names.size()) {
            actions->raise(names->names[follower->reached_action]);
        }
        world.resource<ecs::Commands>()->despawn(entity);
    }
}

} // namespace ge::mod::path2d
