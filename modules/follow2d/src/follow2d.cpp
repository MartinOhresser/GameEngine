#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
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

constexpr std::uint32_t no_action = 0xFFFFFFFFU;

/// The value of an action, 0 when it names none.
float action_value(const ecs::World& world, std::uint32_t action) {
    const auto* actions = world.resource<app::Actions>();
    const auto* names = world.resource<app::ActionNames>();
    if (action == no_action || actions == nullptr || names == nullptr ||
        action >= names->names.size()) {
        return 0.0F;
    }
    return actions->value(names->names[action]);
}

/// Turns the way (dir_x, dir_y) towards (x, y), a length of 1, by at most `most` radians.
void turn_towards(Follow& follow, float x, float y, float most) {
    const float now = std::atan2(follow.dir_y, follow.dir_x);
    float apart = std::atan2(y, x) - now;
    constexpr float pi = std::numbers::pi_v<float>;
    while (apart > pi) {
        apart -= 2.0F * pi;
    }
    while (apart < -pi) {
        apart += 2.0F * pi;
    }
    const float turned = now + std::clamp(apart, -most, most);
    follow.dir_x = std::cos(turned);
    follow.dir_y = std::sin(turned);
}

/// How fast the target goes, and the way it last went (kept while it stands still), or the way
/// the actions point; turned round at most way_turn degrees a second.
void track_way(const ecs::World& world, ecs::Entity target, Follow& follow, float step) {
    constexpr float still = 1.0F;
    constexpr float pointing = 0.2F;
    const auto* velocity = world.get<transform2d::Velocity>(target);
    follow.speed = velocity == nullptr ? 0.0F : std::hypot(velocity->x, velocity->y);
    float x = 0.0F;
    float y = 0.0F;
    if (follow.way_x != no_action || follow.way_y != no_action) {
        x = action_value(world, follow.way_x);
        y = action_value(world, follow.way_y);
    } else if (velocity != nullptr) {
        x = velocity->x;
        y = velocity->y;
    }
    const float length = std::hypot(x, y);
    const bool actions = follow.way_x != no_action || follow.way_y != no_action;
    if (length <= (actions ? pointing : still)) {
        return;
    }
    x /= length;
    y /= length;
    if (follow.way_turn <= 0.0F) {
        follow.dir_x = x;
        follow.dir_y = y;
        return;
    }
    turn_towards(follow, x, y, follow.way_turn * std::numbers::pi_v<float> / 180.0F * step);
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
            track_way(world, target, follow, step);
            float aim_x = place->x + offset_x + follow.ahead * follow.dir_x;
            float aim_y = place->y + follow.offset_y + follow.ahead * follow.dir_y;
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
