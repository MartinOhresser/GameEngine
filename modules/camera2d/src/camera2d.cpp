#include "ge/app/render.hpp"
#include "ge/app/time.hpp"
#include "ge/app/viewport.hpp"
#include "ge/mod/camera2d/camera2d.gen.hpp"
#include "ge/mod/transform2d/drawn.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

namespace ge::mod::camera2d {

namespace {

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

/// The CameraTarget seen by a camera: where it is, and how fast it goes along x.
struct Target {
    transform2d::Position position;
    float speed_x = 0.0F;
};

/// The first CameraTarget (the oldest one), with its Velocity when it has one.
std::optional<Target> first_target(ecs::World& world) {
    std::optional<Target> found;
    world.each<const CameraTarget, const transform2d::Position>(
        [&](ecs::Entity entity, const CameraTarget&, const transform2d::Position& position) {
            if (!found) {
                const auto* velocity = world.get<transform2d::Velocity>(entity);
                found = Target{position, velocity != nullptr ? velocity->x : 0.0F};
            }
        });
    return found;
}

/// One step of an axis towards `goal`: nothing while the goal is within `dead` of the camera,
/// else `stiffness` of the way to the edge of that dead zone.
float towards(float now, float goal, float dead, float stiffness) {
    const float gap = goal - now;
    const float reach = std::max(dead, 0.0F);
    if (std::abs(gap) <= reach) {
        return now;
    }
    const float aim = gap > 0.0F ? goal - reach : goal + reach;
    return now + (aim - now) * stiffness;
}

/// `centre` moved so that a view of half size `half` stays within [low, high]; centred when
/// the view is bigger than the area.
float bounded(float centre, float half, float low, float high) {
    if (high - low <= half * 2.0F) {
        return (low + high) / 2.0F;
    }
    return std::clamp(centre, low + half, high - half);
}

/// How far a shaken camera is moved along one axis at `seconds`, as a share of its strength (in
/// [-1, 1]): waves at unrelated rates, so the shake never looks regular. `phase` tells the axes
/// apart.
float shaken(float seconds, float speed, float phase) {
    const float a = seconds * speed * 2.0F * std::numbers::pi_v<float>;
    return (0.6F * std::sin(a + phase)) + (0.4F * std::sin((2.37F * a) + (1.9F * phase)));
}

/// Speeds below this (units per second) are standing still for the look-ahead.
constexpr float still_speed = 1.0F;

} // namespace

void follow(ecs::World& world) {
    const std::optional<Target> target = first_target(world);
    if (!target) {
        return;
    }
    const auto* viewport = world.resource<app::Viewport>();
    world.each<const CameraFollow, transform2d::Position>(
        [&](ecs::Entity entity, const CameraFollow& follow, transform2d::Position& position) {
            const float stiffness = std::clamp(follow.stiffness, 0.0F, 1.0F);
            if (follow.follow_x) {
                float ahead = 0.0F;
                if (std::abs(target->speed_x) > still_speed) {
                    ahead = target->speed_x > 0.0F ? follow.look_ahead : -follow.look_ahead;
                }
                const float goal = target->position.x + follow.offset_x + ahead;
                const float next = towards(position.x, goal, follow.dead_x, stiffness);
                position.x = limited(position.x, next, follow.only_left, follow.only_right);
            }
            if (follow.follow_y) {
                const float goal = target->position.y + follow.offset_y;
                const float next = towards(position.y, goal, follow.dead_y, stiffness);
                position.y = limited(position.y, next, follow.only_up, follow.only_down);
            }
            if (follow.bounded) {
                const auto* camera = world.get<Camera>(entity);
                const float zoom = camera != nullptr && camera->zoom > 0.0F ? camera->zoom : 1.0F;
                const float half_width =
                    viewport != nullptr ? static_cast<float>(viewport->width) / 2.0F / zoom : 0.0F;
                const float half_height =
                    viewport != nullptr ? static_cast<float>(viewport->height) / 2.0F / zoom : 0.0F;
                position.x = bounded(position.x, half_width, follow.bound_left, follow.bound_right);
                position.y =
                    bounded(position.y, half_height, follow.bound_top, follow.bound_bottom);
            }
        });
}

void settle(ecs::World& world) {
    const float step = world.resource<app::Time>()->fixed_delta;
    world.each<CameraShake>([step](ecs::Entity, CameraShake& shake) {
        shake.strength = std::max(shake.strength - (shake.decay * step), 0.0F);
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
                transform2d::Position position = transform2d::drawn_position(world, entity, now);
                if (const auto* shake = world.get<CameraShake>(entity);
                    shake != nullptr && shake->strength > 0.0F) {
                    const auto* time = world.resource<app::Time>();
                    const float seconds =
                        time != nullptr ? static_cast<float>(time->tick) * time->fixed_delta : 0.0F;
                    position.x += shake->strength * shaken(seconds, shake->speed, 0.0F);
                    position.y += shake->strength * shaken(seconds, shake->speed, 2.1F);
                }
                backend->renderer().set_camera({position.x, position.y, camera.zoom});
            }
        });
}

} // namespace ge::mod::camera2d
