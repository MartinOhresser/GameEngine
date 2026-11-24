#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/app/viewport.hpp"
#include "ge/mod/aim2d/aim2d.gen.hpp"
#include "ge/mod/camera2d/camera2d.gen.hpp"
#include "ge/mod/tag/find.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <cmath>
#include <cstdint>
#include <string_view>

namespace ge::mod::aim2d {

namespace {

constexpr std::uint32_t none = 0xFFFFFFFFU;

/// Where the window's centre is in the world and how enlarged: the oldest camera.
struct View {
    bool has_camera = false;
    float x = 0.0F;
    float y = 0.0F;
    float zoom = 1.0F;
    float width = 0.0F;
    float height = 0.0F;

    explicit View(ecs::World& world) {
        if (const auto* viewport = world.resource<app::Viewport>(); viewport != nullptr) {
            width = static_cast<float>(viewport->width);
            height = static_cast<float>(viewport->height);
        }
        world.each<const camera2d::Camera, const transform2d::Position>(
            [this](ecs::Entity, const camera2d::Camera& camera,
                   const transform2d::Position& position) {
                if (!has_camera) {
                    has_camera = true;
                    x = position.x;
                    y = position.y;
                    zoom = camera.zoom > 0.0F ? camera.zoom : 1.0F;
                }
            });
    }

    /// The world point under a point of the window.
    void to_world(float& px, float& py) const {
        if (!has_camera) {
            return; // without a camera, one unit is one pixel from the top left corner
        }
        px = (px - width / 2.0F) / zoom + x;
        py = (py - height / 2.0F) / zoom + y;
    }
};

float pointer(const app::Actions& actions, const app::ActionNames* names, std::uint32_t action,
              std::string_view fallback) {
    if (action != none && names != nullptr && action < names->names.size()) {
        return actions.value(names->names[action]);
    }
    return actions.value(fallback);
}

} // namespace

void aim(ecs::World& world) {
    const auto* actions = world.resource<app::Actions>();
    if (actions == nullptr) {
        return;
    }
    const auto* names = world.resource<app::ActionNames>();
    const View view(world);
    const tag::Tagged tags(world);
    world.each<Aim, transform2d::Position>([&](ecs::Entity entity, Aim& aimed,
                                               transform2d::Position& position) {
        float px = pointer(*actions, names, aimed.pointer_x, "pointer_x");
        float py = pointer(*actions, names, aimed.pointer_y, "pointer_y");
        view.to_world(px, py);
        aimed.x = px;
        aimed.y = py;
        float target_x = px;
        float target_y = py;
        const ecs::Entity origin = aimed.from == none ? ecs::Entity{} : tags.find(aimed.from);
        const auto* from = origin.is_null() ? nullptr : world.get<transform2d::Position>(origin);
        if (from != nullptr) {
            const float dx = px - from->x;
            const float dy = py - from->y;
            const float distance = std::sqrt(dx * dx + dy * dy);
            aimed.distance = distance;
            aimed.dir_x = distance > 0.0F ? dx / distance : 1.0F;
            aimed.dir_y = distance > 0.0F ? dy / distance : 0.0F;
            if (aimed.range > 0.0F && distance > aimed.range) {
                target_x = from->x + aimed.dir_x * aimed.range;
                target_y = from->y + aimed.dir_y * aimed.range;
            }
        }
        aimed.reach_x = target_x;
        aimed.reach_y = target_y;
        if (!aimed.lock || world.has<transform2d::Disabled>(entity)) {
            position.x = target_x;
            position.y = target_y;
        }
    });
}

} // namespace ge::mod::aim2d
