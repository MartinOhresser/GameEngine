#include "ge/ecs/commands.hpp"
#include "ge/mod/collision2d/collision2d.gen.hpp"
#include "ge/mod/combat2d/combat2d.gen.hpp"
#include "ge/mod/health/damage.hpp"
#include "ge/mod/health/health.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace ge::mod::combat2d {

namespace {

/// A collider as a box, with the entity it belongs to and its layer.
struct Box {
    ecs::Entity entity;
    float min_x = 0.0F;
    float max_x = 0.0F;
    float min_y = 0.0F;
    float max_y = 0.0F;
    std::uint32_t layer = 0;

    [[nodiscard]] float centre_x() const { return (min_x + max_x) / 2.0F; }
    [[nodiscard]] float centre_y() const { return (min_y + max_y) / 2.0F; }
};

bool overlap(const Box& a, const Box& b) {
    return a.min_x < b.max_x && b.min_x < a.max_x && a.min_y < b.max_y && b.min_y < a.max_y;
}

Box box_of(ecs::Entity entity, const collision2d::Collider& collider,
           const transform2d::Position& position) {
    const float half_width = collider.width / 2.0F;
    const float half_height = collider.height / 2.0F;
    return Box{entity,
               position.x - half_width,
               position.x + half_width,
               position.y - half_height,
               position.y + half_height,
               collider.layer};
}

/// What can be hurt: every entity with a Health, a Collider and a Position that is not dead.
std::vector<Box> hurtable(ecs::World& world) {
    std::vector<Box> boxes;
    world.each<const health::Health, const collision2d::Collider, const transform2d::Position>(
        [&](ecs::Entity entity, const health::Health&, const collision2d::Collider& collider,
            const transform2d::Position& position) {
            if (!world.has<health::Dead>(entity) && !world.has<transform2d::Disabled>(entity)) {
                boxes.push_back(box_of(entity, collider, position));
            }
        });
    return boxes;
}

/// Slows an entity to `factor` of its speed for `ticks` ticks: a stronger slow or a longer one
/// that is already there stays.
void slow(ecs::World& world, ecs::Entity entity, float factor, std::uint32_t ticks) {
    if (auto* now = world.get<transform2d::SpeedScale>(entity)) {
        now->factor = std::min(now->factor, factor);
        now->ticks = std::max(now->ticks, ticks);
    } else {
        world.resource<ecs::Commands>()->insert(entity, transform2d::SpeedScale{factor, ticks});
    }
}

/// What a projectile hit, to be paid after the loop: damage per entity (added up) and the slows.
struct Slow {
    ecs::Entity entity;
    float factor = 1.0F;
    std::uint32_t ticks = 0;
};

struct Hits {
    std::vector<std::pair<ecs::Entity, std::int32_t>> damage;
    std::vector<Slow> slows;

    void add(ecs::Entity entity, std::int32_t amount) {
        for (auto& item : damage) {
            if (item.first == entity) {
                item.second += amount;
                return;
            }
        }
        damage.emplace_back(entity, amount);
    }
};

/// The entities a hit at `shot` reaches: the one it overlaps, and with a splash those near it.
std::vector<ecs::Entity> reached(const Box& shot, const Box& primary, const Projectile& projectile,
                                 const std::vector<Box>& targets) {
    std::vector<ecs::Entity> list{primary.entity};
    if (projectile.splash_radius <= 0.0F) {
        return list;
    }
    for (const Box& other : targets) {
        const float dx = other.centre_x() - shot.centre_x();
        const float dy = other.centre_y() - shot.centre_y();
        if (other.entity != primary.entity && (other.layer & projectile.layers) != 0U &&
            std::sqrt(dx * dx + dy * dy) <= projectile.splash_radius) {
            list.push_back(other.entity);
        }
    }
    return list;
}

/// The first target of the list that the projectile overlaps and may hit (not the one it has just
/// gone through).
const Box* first_hit(const Box& shot, const Projectile& projectile,
                     const std::vector<Box>& targets) {
    for (const Box& target : targets) {
        if ((target.layer & projectile.layers) != 0U &&
            target.entity.to_link() != projectile.last_hit && overlap(shot, target)) {
            return &target;
        }
    }
    return nullptr;
}

} // namespace

void steer(ecs::World& world) {
    const std::vector<Box> targets = hurtable(world);
    if (targets.empty()) {
        return;
    }
    world.each<const Projectile, transform2d::Velocity, const transform2d::Position>(
        [&](ecs::Entity, const Projectile& projectile, transform2d::Velocity& velocity,
            const transform2d::Position& position) {
            const float speed = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
            if (projectile.homing <= 0.0F || speed <= 0.0F) {
                return;
            }
            const Box* nearest = nullptr;
            float best = 0.0F;
            for (const Box& target : targets) {
                const float dx = target.centre_x() - position.x;
                const float dy = target.centre_y() - position.y;
                const float distance = dx * dx + dy * dy;
                if ((target.layer & projectile.layers) != 0U &&
                    (nearest == nullptr || distance < best)) {
                    nearest = &target;
                    best = distance;
                }
            }
            if (nearest == nullptr || best <= 0.0F) {
                return;
            }
            const float share = std::min(projectile.homing, 1.0F);
            const float length = std::sqrt(best);
            const float want_x = (nearest->centre_x() - position.x) / length;
            const float want_y = (nearest->centre_y() - position.y) / length;
            const float mix_x = velocity.x / speed * (1.0F - share) + want_x * share;
            const float mix_y = velocity.y / speed * (1.0F - share) + want_y * share;
            const float mix = std::sqrt(mix_x * mix_x + mix_y * mix_y);
            if (mix > 0.0F) {
                velocity.x = mix_x / mix * speed;
                velocity.y = mix_y / mix * speed;
            }
        });
}

void shots(ecs::World& world) {
    const std::vector<Box> targets = hurtable(world);
    if (targets.empty()) {
        return;
    }
    Hits hits;
    std::vector<ecs::Entity> spent;
    world.each<Projectile, const collision2d::Collider, const transform2d::Position>(
        [&](ecs::Entity entity, Projectile& projectile, const collision2d::Collider& collider,
            const transform2d::Position& position) {
            const Box shot = box_of(entity, collider, position);
            const Box* primary = first_hit(shot, projectile, targets);
            if (primary == nullptr) {
                return; // one hit per tick: the first target in entity order
            }
            for (const ecs::Entity victim : reached(shot, *primary, projectile, targets)) {
                hits.add(victim, projectile.damage);
                if (projectile.on_hit_speed_factor < 1.0F) {
                    hits.slows.push_back(
                        {victim, projectile.on_hit_speed_factor, projectile.on_hit_speed_ticks});
                }
            }
            projectile.last_hit = primary->entity.to_link();
            if (projectile.pierce > 0U) {
                --projectile.pierce;
            } else {
                spent.push_back(entity);
            }
        });
    for (const auto& [victim, amount] : hits.damage) {
        health::deal(world, victim, amount);
    }
    for (const Slow& item : hits.slows) {
        slow(world, item.entity, item.factor, item.ticks);
    }
    auto& commands = *world.resource<ecs::Commands>();
    for (const ecs::Entity entity : spent) {
        commands.despawn(entity);
    }
}

void touch(ecs::World& world) {
    const std::vector<Box> targets = hurtable(world);
    if (targets.empty()) {
        return;
    }
    std::vector<std::pair<ecs::Entity, std::int32_t>> dealt;
    world.each<ContactDamage, const collision2d::Collider, const transform2d::Position>(
        [&](ecs::Entity entity, ContactDamage& source, const collision2d::Collider& collider,
            const transform2d::Position& position) {
            if (world.has<transform2d::Disabled>(entity)) {
                return;
            }
            if (source.wait > 0U) {
                --source.wait;
                return;
            }
            const Box body = box_of(entity, collider, position);
            bool hurt_any = false;
            for (const Box& target : targets) {
                if (target.entity != entity && (target.layer & source.layers) != 0U &&
                    overlap(body, target)) {
                    dealt.emplace_back(target.entity, source.amount);
                    hurt_any = true;
                }
            }
            if (hurt_any) {
                source.wait = source.cooldown;
            }
        });
    for (const auto& [victim, amount] : dealt) {
        health::deal(world, victim, amount);
    }
}

} // namespace ge::mod::combat2d
