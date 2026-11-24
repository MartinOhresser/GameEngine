#include "ge/app/time.hpp"
#include "ge/core/hash.hpp"
#include "ge/ecs/commands.hpp"
#include "ge/mod/attractor2d/attractor2d.gen.hpp"
#include "ge/mod/collision2d/collision2d.gen.hpp"
#include "ge/mod/reserve/reserve.gen.hpp"
#include "ge/mod/reserve/stock.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace ge::mod::attractor2d {

namespace {

constexpr std::uint32_t none = 0xFFFFFFFFU;

/// What the wells that give when they stop are keeping, by well and by kind.
struct Kept {
    std::map<std::uint64_t, std::map<std::uint32_t, float>> wells;

    void hash(Hasher& hasher) const {
        for (const auto& [link, kinds] : wells) {
            hasher.add(link);
            for (const auto& [kind, amount] : kinds) {
                hasher.add(kind);
                hasher.add(amount);
            }
        }
    }
};

Kept& kept_of(ecs::World& world) {
    if (auto* kept = world.resource<Kept>(); kept != nullptr) {
        return *kept;
    }
    return world.emplace_resource<Kept>();
}

struct Well {
    ecs::Entity entity;
    float x = 0.0F;
    float y = 0.0F;
    Attractor* attractor = nullptr;
};

void pull_around(ecs::World& world, const Well& well, float tick) {
    const float radius = well.attractor->radius;
    world.each<transform2d::Velocity, const transform2d::Position, const collision2d::Collider>(
        [&](ecs::Entity entity, transform2d::Velocity& velocity,
            const transform2d::Position& at, const collision2d::Collider& collider) {
            if (entity == well.entity || (collider.layer & well.attractor->pulls) == 0U) {
                return;
            }
            const float dx = well.x - at.x;
            const float dy = well.y - at.y;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance >= radius || distance <= 0.0F) {
                return;
            }
            const float strength = well.attractor->pull * (1.0F - distance / radius) * tick;
            velocity.x += dx / distance * strength;
            velocity.y += dy / distance * strength;
        });
}

void swallow(ecs::World& world, const Well& well, std::set<std::uint64_t>& gone, Kept& kept) {
    auto* commands = world.resource<ecs::Commands>();
    Attractor& attractor = *well.attractor;
    world.each<const Absorbable, const transform2d::Position>(
        [&](ecs::Entity entity, const Absorbable& absorbable, const transform2d::Position& at) {
            const float dx = well.x - at.x;
            const float dy = well.y - at.y;
            if (entity == well.entity || gone.contains(entity.to_link()) ||
                absorbable.mass > attractor.radius ||
                dx * dx + dy * dy > attractor.heart * attractor.heart) {
                return;
            }
            gone.insert(entity.to_link());
            commands->despawn(entity);
            attractor.swallowed += 1U;
            const auto* held = world.get<reserve::Yield>(entity);
            if (held == nullptr) {
                return;
            }
            const std::uint32_t kind = attractor.convert == none ? held->kind : attractor.convert;
            if (attractor.deferred) {
                kept.wells[well.entity.to_link()][kind] += held->amount;
                attractor.pending += held->amount;
            } else {
                reserve::give(world, kind, held->amount);
            }
        });
}

/// A well that stops gives what it kept.
void release(ecs::World& world, const Well& well, Kept& kept) {
    const auto found = kept.wells.find(well.entity.to_link());
    if (found == kept.wells.end()) {
        return;
    }
    for (const auto& [kind, amount] : found->second) {
        reserve::give(world, kind, amount);
    }
    kept.wells.erase(found);
    well.attractor->pending = 0.0F;
}

} // namespace

void pull(ecs::World& world) {
    const float tick = world.resource<app::Time>()->fixed_delta *
                       world.resource<transform2d::Params>()->speed_scale;
    Kept& kept = kept_of(world);
    std::vector<Well> wells;
    world.each<Attractor, const transform2d::Position>(
        [&](ecs::Entity entity, Attractor& attractor, const transform2d::Position& at) {
            wells.push_back(Well{entity, at.x, at.y, &attractor});
        });
    std::set<std::uint64_t> gone;
    for (const Well& well : wells) {
        Attractor& attractor = *well.attractor;
        const bool working = !world.has<transform2d::Disabled>(well.entity);
        if (!working) {
            release(world, well, kept);
            attractor.radius = std::max(attractor.radius - attractor.shrink * tick, 0.0F);
            attractor.heart = attractor.radius * attractor.inner;
            continue;
        }
        attractor.radius = attractor.radius < attractor.start_radius
                               ? attractor.start_radius
                               : std::min(attractor.radius + attractor.grow * tick,
                                          std::max(attractor.max_radius, attractor.start_radius));
        attractor.heart = attractor.radius * attractor.inner;
        pull_around(world, well, tick);
        swallow(world, well, gone, kept);
    }
    std::erase_if(kept.wells, [&world](const auto& entry) {
        return !world.is_alive(ecs::Entity::from_link(entry.first));
    });
}

} // namespace ge::mod::attractor2d
