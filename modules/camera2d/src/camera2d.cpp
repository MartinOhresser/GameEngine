#include "ge/app/render.hpp"
#include "ge/app/viewport.hpp"
#include "ge/mod/camera2d/camera2d.gen.hpp"
#include "ge/mod/transform2d/drawn.hpp"
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

void fit(ecs::World& world) {
    const auto* viewport = world.resource<app::Viewport>();
    if (viewport == nullptr) {
        return;
    }
    const auto screen_width = static_cast<float>(viewport->width);
    const auto screen_height = static_cast<float>(viewport->height);
    world.each<const CameraFit, Camera, transform2d::Position>(
        [&](ecs::Entity, const CameraFit& area, Camera& camera, transform2d::Position& position) {
            const float room_x =
                std::max(screen_width - area.margin_left - area.margin_right, 1.0F);
            const float room_y =
                std::max(screen_height - area.margin_top - area.margin_bottom, 1.0F);
            float zoom = std::min(room_x / std::max(area.width, 0.001F),
                                  room_y / std::max(area.height, 0.001F));
            if (area.max_zoom > 0.0F) {
                zoom = std::min(zoom, area.max_zoom);
            }
            camera.zoom = zoom;
            // The area's centre goes to the middle of what the margins leave.
            const float middle_x = (area.margin_left + screen_width - area.margin_right) / 2.0F;
            const float middle_y = (area.margin_top + screen_height - area.margin_bottom) / 2.0F;
            position.x = area.center_x - (middle_x - screen_width / 2.0F) / zoom;
            position.y = area.center_y - (middle_y - screen_height / 2.0F) / zoom;
        });
}

void apply(ecs::World& world) {
    auto* backend = world.resource<app::RenderBackend>();
    if (backend == nullptr) {
        return;
    }
    bool found = false;
    world.each<const Camera, const transform2d::Position>(
        [&](ecs::Entity entity, const Camera& camera, const transform2d::Position& now) {
            if (!found) {
                found = true;
                const transform2d::Position position =
                    transform2d::drawn_position(world, entity, now);
                backend->renderer().set_camera({position.x, position.y, camera.zoom});
            }
        });
}

} // namespace ge::mod::camera2d
