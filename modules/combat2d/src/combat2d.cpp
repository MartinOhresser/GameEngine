#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/app/viewport.hpp"
#include "ge/mod/camera2d/camera2d.gen.hpp"
#include "ge/mod/collision2d/collision2d.gen.hpp"
#include "ge/mod/combat2d/combat2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace ge::mod::combat2d {

namespace {

/// A collider as a box, for the overlaps below.
struct Box {
    ecs::Entity entity;
    float min_x = 0.0F;
    float max_x = 0.0F;
    float min_y = 0.0F;
    float max_y = 0.0F;
};

bool overlap(const Box& a, const Box& b) {
    return a.min_x < b.max_x && b.min_x < a.max_x && a.min_y < b.max_y && b.min_y < a.max_y;
}

Box box_of(ecs::Entity entity, const collision2d::Collider& collider,
           const transform2d::Position& position) {
    const float half_width = collider.width / 2.0F;
    const float half_height = collider.height / 2.0F;
    return Box{entity, position.x - half_width, position.x + half_width, position.y - half_height,
               position.y + half_height};
}

/// The y of the bottom edge of the screen, from the first camera.
std::optional<float> screen_bottom(ecs::World& world) {
    const auto* viewport = world.resource<app::Viewport>();
    std::optional<float> bottom;
    if (viewport == nullptr) {
        return bottom;
    }
    world.each<const camera2d::Camera, const transform2d::Position>(
        [&](ecs::Entity, const camera2d::Camera& camera, const transform2d::Position& position) {
            if (!bottom) {
                const float zoom = camera.zoom > 0.0F ? camera.zoom : 1.0F;
                bottom = position.y + static_cast<float>(viewport->height) / (2.0F * zoom);
            }
        });
    return bottom;
}

} // namespace

void shots(ecs::World& world) {
    std::vector<Box> targets;
    world.each<const Target, const collision2d::Collider, const transform2d::Position>(
        [&targets](ecs::Entity entity, const Target&, const collision2d::Collider& collider,
                   const transform2d::Position& position) {
            targets.push_back(box_of(entity, collider, position));
        });
    if (targets.empty()) {
        return;
    }
    std::vector<ecs::Entity> spent;
    world.each<const Projectile, const collision2d::Collider, const transform2d::Position>(
        [&](ecs::Entity entity, const Projectile& projectile, const collision2d::Collider& collider,
            const transform2d::Position& position) {
            const Box shot = box_of(entity, collider, position);
            for (const Box& target : targets) {
                auto* life = world.get<Target>(target.entity);
                if (life->health > 0 && overlap(shot, target)) {
                    life->health -= projectile.damage;
                    spent.push_back(entity);
                    return; // one hit per projectile: the first target in entity order
                }
            }
        });
    auto& commands = *world.resource<ecs::Commands>();
    for (const Box& target : targets) {
        if (world.get<Target>(target.entity)->health <= 0) {
            commands.despawn(target.entity);
        }
    }
    for (const ecs::Entity entity : spent) {
        commands.despawn(entity);
    }
}

void hurt(ecs::World& world) {
    const std::optional<float> bottom = screen_bottom(world);
    world.each<Mortal, const transform2d::Position>([&](ecs::Entity entity, Mortal& mortal,
                                                        const transform2d::Position& position) {
        const auto* contacts = world.get<collision2d::Contacts>(entity);
        const bool touched = contacts != nullptr && (contacts->layers & mortal.hurt_layers) != 0U;
        const bool fell = mortal.fall_death && bottom && position.y > *bottom + mortal.fall_margin;
        if (touched || fell) {
            mortal.dead = true;
        }
    });
}

void announce(ecs::World& world) {
    auto* actions = world.resource<app::Actions>();
    const auto* names = world.resource<app::ActionNames>();
    world.each<Mortal>([&](ecs::Entity, Mortal& mortal) {
        if (!mortal.dead || mortal.announced) {
            return;
        }
        mortal.announced = true;
        if (actions != nullptr && names != nullptr &&
            static_cast<std::size_t>(mortal.on_death) < names->names.size()) {
            actions->raise(names->names[mortal.on_death]);
        }
    });
}

} // namespace ge::mod::combat2d
