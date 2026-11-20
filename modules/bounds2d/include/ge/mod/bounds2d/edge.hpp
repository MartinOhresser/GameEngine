#pragma once

// Where the edges of the screen are, for the modules that depend on bounds2d. Header only.

#include "ge/app/direction.hpp"
#include "ge/app/viewport.hpp"
#include "ge/ecs/world.hpp"
#include "ge/mod/camera2d/camera2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <optional>

namespace ge::mod::bounds2d {

/// How far the edge of the screen that `direction` points to is, along that direction (see
/// app::Direction: for down it is the y of the bottom edge, for up minus the y of the top edge),
/// seen by the first camera. Nothing without a camera or a viewport.
inline std::optional<float> screen_edge(ecs::World& world, const app::Direction& direction) {
    std::optional<float> edge;
    const auto* viewport = world.resource<app::Viewport>();
    if (viewport == nullptr) {
        return edge;
    }
    world.each<const camera2d::Camera, const transform2d::Position>(
        [&](ecs::Entity, const camera2d::Camera& camera, const transform2d::Position& position) {
            if (!edge) {
                const float zoom = camera.zoom > 0.0F ? camera.zoom : 1.0F;
                const auto size =
                    static_cast<float>(direction.vertical ? viewport->height : viewport->width);
                edge = direction.along(position.x, position.y) + size / (2.0F * zoom);
            }
        });
    return edge;
}

} // namespace ge::mod::bounds2d
