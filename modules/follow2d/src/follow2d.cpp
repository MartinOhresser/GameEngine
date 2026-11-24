#include "ge/app/time.hpp"
#include "ge/mod/follow2d/follow2d.gen.hpp"
#include "ge/mod/tag/find.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ge::mod::follow2d {

namespace {

/// The entity a Follow goes after: its owner, or the one that answers to its tag.
ecs::Entity target_of(const ecs::World& world, const tag::Tagged& tagged, ecs::Entity entity,
                      const Follow& follow) {
    if (!follow.owner) {
        return tagged.find(follow.target);
    }
    const auto* owner = world.get<transform2d::Owner>(entity);
    return owner == nullptr ? ecs::Entity{} : ecs::Entity::from_link(owner->entity);
}

/// The way the target goes along x (1 right, -1 left), kept while it stands still.
void track_side(const ecs::World& world, ecs::Entity target, Follow& follow) {
    constexpr float still = 1.0F;
    if (const auto* velocity = world.get<transform2d::Velocity>(target); velocity != nullptr) {
        if (velocity->x > still) {
            follow.side = 1;
        } else if (velocity->x < -still) {
            follow.side = -1;
        }
    }
}

} // namespace

void follow(ecs::World& world) {
    const tag::Tagged tagged = tag::tagged(world);
    const float step = world.resource<app::Time>()->fixed_delta;
    world.each<Follow, transform2d::Position>(
        [&](ecs::Entity entity, Follow& follow, transform2d::Position& position) {
            const ecs::Entity target = target_of(world, tagged, entity, follow);
            const auto* place = target.is_null() || !world.is_alive(target)
                                    ? nullptr
                                    : world.get<transform2d::Position>(target);
            if (place == nullptr) {
                return;
            }
            float offset_x = follow.offset_x;
            if (follow.turn) {
                track_side(world, target, follow);
                offset_x *= follow.side < 0 ? -1.0F : 1.0F;
            }
            float aim_x = place->x + offset_x;
            float aim_y = place->y + follow.offset_y;
            if (follow.wave_period > 0.0F && (follow.wave_x != 0.0F || follow.wave_y != 0.0F)) {
                follow.time = std::fmod(follow.time + step, follow.wave_period);
                const float turn_angle =
                    2.0F * std::numbers::pi_v<float> * follow.time / follow.wave_period;
                aim_x += follow.wave_x * std::sin(turn_angle);
                aim_y += follow.wave_y * std::sin(2.0F * turn_angle);
            }
            const float share = std::clamp(follow.stiffness, 0.0F, 1.0F);
            position.x += (aim_x - position.x) * share;
            position.y += (aim_y - position.y) * share;
        });
}

} // namespace ge::mod::follow2d
