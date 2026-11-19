#include "ge/app/render.hpp"
#include "ge/mod/camera2d/camera2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <optional>

namespace ge::mod::camera2d {

namespace {

/// The Position of the first entity that has `Marker` (the oldest one).
template <typename Marker>
std::optional<transform2d::Position> first_position(ecs::World& world) {
    std::optional<transform2d::Position> found;
    world.each<const Marker, const transform2d::Position>(
        [&found](ecs::Entity, const Marker&, const transform2d::Position& position) {
            if (!found) {
                found = position;
            }
        });
    return found;
}

/// Where an axis goes: `next`, unless the camera may not move that way. `smaller_only` keeps it
/// from ever growing (it only goes to smaller numbers: up or left), `bigger_only` the contrary.
float limited(float now, float next, bool smaller_only, bool bigger_only) {
    if (smaller_only) {
        next = std::min(now, next);
    }
    if (bigger_only) {
        next = std::max(now, next);
    }
    return next;
}

} // namespace

void follow(ecs::World& world) {
    const std::optional<transform2d::Position> target = first_position<CameraTarget>(world);
    if (!target) {
        return;
    }
    world.each<const CameraFollow, transform2d::Position>(
        [&target](ecs::Entity, const CameraFollow& follow, transform2d::Position& position) {
            const float stiffness = std::clamp(follow.stiffness, 0.0F, 1.0F);
            if (follow.follow_x) {
                const float goal = target->x + follow.offset_x;
                const float next = position.x + (goal - position.x) * stiffness;
                position.x = limited(position.x, next, follow.only_left, follow.only_right);
            }
            if (follow.follow_y) {
                const float goal = target->y + follow.offset_y;
                const float next = position.y + (goal - position.y) * stiffness;
                position.y = limited(position.y, next, follow.only_up, follow.only_down);
            }
        });
}

void apply(ecs::World& world) {
    auto* backend = world.resource<app::RenderBackend>();
    if (backend == nullptr) {
        return;
    }
    bool found = false;
    world.each<const Camera, const transform2d::Position>(
        [&](ecs::Entity, const Camera& camera, const transform2d::Position& position) {
            if (!found) {
                found = true;
                backend->renderer().set_camera({position.x, position.y, camera.zoom});
            }
        });
}

} // namespace ge::mod::camera2d
