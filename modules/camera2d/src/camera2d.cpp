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

} // namespace

void follow(ecs::World& world) {
    const std::optional<transform2d::Position> target = first_position<CameraTarget>(world);
    if (!target) {
        return;
    }
    world.each<const CameraFollow, transform2d::Position>(
        [&target](ecs::Entity, const CameraFollow& follow, transform2d::Position& position) {
            const float stiffness = std::clamp(follow.stiffness, 0.0F, 1.0F);
            position.x += (target->x - position.x) * stiffness;
            position.y += (target->y - position.y) * stiffness;
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
