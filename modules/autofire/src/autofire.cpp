#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/app/spawn.hpp"
#include "ge/mod/autofire/autofire.gen.hpp"
#include "ge/mod/collision2d/collision2d.gen.hpp"
#include "ge/mod/health/health.gen.hpp"
#include "ge/mod/path2d/path2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ge::mod::autofire {

namespace {

// 0 (the default) is the closest: what no other mode picks.
constexpr std::uint32_t by_first = 1;
constexpr std::uint32_t by_last = 2;
constexpr std::uint32_t by_strength = 3;
constexpr std::uint32_t by_weakness = 4;

/// What a shooter could shoot at.
struct Candidate {
    float x = 0.0F;
    float y = 0.0F;
    float distance = 0.0F;
    float progress = 0.0F;
    std::int32_t health = 0;
    std::uint32_t layer = 0;
};

std::vector<Candidate> candidates(ecs::World& world) {
    std::vector<Candidate> list;
    world.each<const health::Health, const collision2d::Collider, const transform2d::Position>(
        [&](ecs::Entity entity, const health::Health& life, const collision2d::Collider& collider,
            const transform2d::Position& position) {
            if (world.has<health::Dead>(entity) || world.has<transform2d::Disabled>(entity)) {
                return;
            }
            const auto* follower = world.get<path2d::PathFollower>(entity);
            list.push_back({position.x, position.y, 0.0F,
                            follower == nullptr ? 0.0F : follower->progress, life.amount,
                            collider.layer});
        });
    return list;
}

/// Whether `a` is a better target than `b` for the way of choosing (both in range).
bool better(std::uint32_t targeting, const Candidate& a, const Candidate& b) {
    switch (targeting) {
    case by_first:
        return a.progress != b.progress ? a.progress > b.progress : a.distance < b.distance;
    case by_last:
        return a.progress != b.progress ? a.progress < b.progress : a.distance < b.distance;
    case by_strength:
        return a.health != b.health ? a.health > b.health : a.distance < b.distance;
    case by_weakness:
        return a.health != b.health ? a.health < b.health : a.distance < b.distance;
    default:
        return a.distance < b.distance;
    }
}

std::optional<Candidate> choose(const std::vector<Candidate>& list, const AutoFire& gun,
                                const transform2d::Position& from) {
    std::optional<Candidate> best;
    for (const Candidate& item : list) {
        Candidate seen = item;
        const float dx = item.x - from.x;
        const float dy = item.y - from.y;
        seen.distance = std::sqrt(dx * dx + dy * dy);
        if ((item.layer & gun.target_layer) == 0U || seen.distance > gun.range) {
            continue;
        }
        if (!best || better(gun.targeting, seen, *best)) {
            best = seen;
        }
    }
    return best;
}

} // namespace

void aim_and_fire(ecs::World& world) {
    auto* requests = world.resource<app::SpawnRequests>();
    if (requests == nullptr) {
        return;
    }
    const auto* names = world.resource<app::ActionNames>();
    auto* actions = world.resource<app::Actions>();
    const std::vector<Candidate> list = candidates(world);
    world.each<AutoFire, const transform2d::Position>(
        [&](ecs::Entity entity, AutoFire& gun, const transform2d::Position& position) {
            if (world.has<transform2d::Disabled>(entity)) {
                return;
            }
            if (gun.wait > 0U) {
                --gun.wait;
                return;
            }
            const std::optional<Candidate> target = choose(list, gun, position);
            if (!target || target->distance <= 0.0F) {
                return;
            }
            const float dx = (target->x - position.x) / target->distance;
            const float dy = (target->y - position.y) / target->distance;
            requests->spawn_moving(gun.prefab, position.x, position.y, dx * gun.projectile_speed,
                                   dy * gun.projectile_speed);
            gun.wait = gun.cooldown;
            if (actions != nullptr && names != nullptr &&
                static_cast<std::size_t>(gun.action) < names->names.size()) {
                actions->raise(names->names[gun.action]);
            }
        });
}

} // namespace ge::mod::autofire
