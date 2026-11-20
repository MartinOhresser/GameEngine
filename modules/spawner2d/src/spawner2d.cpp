#include "ge/app/direction.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/app/spawn.hpp"
#include "ge/app/viewport.hpp"
#include "ge/core/random.hpp"
#include "ge/mod/bounds2d/edge.hpp"
#include "ge/mod/camera2d/camera2d.gen.hpp"
#include "ge/mod/spawner2d/spawner2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace ge::mod::spawner2d {

namespace {

/// What a prefab field holds when it names nothing.
constexpr std::uint32_t no_prefab = 0xFFFFFFFFU;

/// Most rows one generator builds in one tick: a camera that jumped far must not freeze the game.
constexpr int max_rows_per_tick = 64;

/// `first` at the bottom of the ramp, `last` at the top; a negative `last` means "same as first".
float ramped(float first, float last, float progress) {
    return last < 0.0F ? first : first + (last - first) * progress;
}

/// How far up the ramp a row at `distance` from the origin is, from 0 to 1.
float progress_at(float distance, float ramp_distance) {
    if (ramp_distance <= 0.0F) {
        return 1.0F;
    }
    return std::clamp(distance / ramp_distance, 0.0F, 1.0F);
}

/// The prefab of the row: the second alternative first, then the first, then the usual one.
std::uint32_t pick_prefab(const Generator& generator, float progress, Random& random) {
    const float roll = random.unit_float();
    const float second = generator.alt2_prefab == no_prefab
                             ? 0.0F
                             : ramped(generator.alt2_chance, generator.alt2_chance_end, progress);
    if (roll < second) {
        return generator.alt2_prefab;
    }
    const float first = generator.alt_prefab == no_prefab
                            ? 0.0F
                            : ramped(generator.alt_chance, generator.alt_chance_end, progress);
    return roll < second + first ? generator.alt_prefab : generator.prefab;
}

float between(Random& random, float low, float high) {
    return low + random.unit_float() * (high - low);
}

/// Asks for `prefab` at `along` the direction and `across` it, as the x and y of the world.
void spawn_at(app::SpawnRequests& requests, std::uint32_t prefab, const app::Direction& toward,
              float along, float across) {
    if (toward.vertical) {
        requests.spawn(prefab, across, along);
    } else {
        requests.spawn(prefab, along, across);
    }
}

/// Builds one row `along` the direction, `across` it: its prefab and perhaps an extra one.
void build_row(const Generator& generator, const app::Direction& toward, float along, float across,
               float progress, Random& random, app::SpawnRequests& requests) {
    const std::uint32_t prefab = pick_prefab(generator, progress, random);
    spawn_at(requests, prefab, toward, along, across);
    if (generator.extra_prefab == no_prefab ||
        (!generator.extra_on_alt && prefab != generator.prefab)) {
        return;
    }
    const float chance = ramped(generator.extra_chance, generator.extra_chance_end, progress);
    if (random.chance(chance)) {
        const float shift = between(random, -generator.extra_across, generator.extra_across);
        spawn_at(requests, generator.extra_prefab, toward,
                 along + toward.sign * generator.extra_along, across + shift);
    }
}

void run_generator(Generator& generator, float limit, Random& random,
                   app::SpawnRequests& requests) {
    const app::Direction toward = app::Direction::of(generator.toward_x, generator.toward_y);
    // `next` is a coordinate of the world; the rows are laid out in the order of `ahead`, where
    // ahead is bigger.
    for (int rows = 0; rows < max_rows_per_tick && toward.sign * generator.next < limit; ++rows) {
        const float along = generator.next;
        const float distance = toward.sign * (along - generator.origin);
        const float progress = progress_at(distance, generator.ramp_distance);
        const float low = ramped(generator.gap_min, generator.gap_min_end, progress);
        const float high = ramped(generator.gap_max, generator.gap_max_end, progress);
        generator.next += toward.sign * std::max(between(random, low, high), 1.0F);
        const float chance = ramped(generator.row_chance, generator.row_chance_end, progress);
        if (distance >= generator.from_distance && random.chance(chance)) {
            const float across = between(random, generator.across_min, generator.across_max);
            build_row(generator, toward, along, across, progress, random, requests);
        }
    }
}

} // namespace

void generate(ecs::World& world) {
    auto* requests = world.resource<app::SpawnRequests>();
    auto* random = world.resource<Random>();
    if (requests == nullptr || random == nullptr) {
        return;
    }
    world.each<Generator>([&](ecs::Entity, Generator& generator) {
        const app::Direction toward = app::Direction::of(generator.toward_x, generator.toward_y);
        const std::optional<float> edge = bounds2d::screen_edge(world, toward);
        if (edge) {
            run_generator(generator, *edge + generator.ahead, *random, *requests);
        }
    });
}

void shoot(ecs::World& world) {
    auto* requests = world.resource<app::SpawnRequests>();
    const auto* actions = world.resource<app::Actions>();
    const auto* names = world.resource<app::ActionNames>();
    if (requests == nullptr || actions == nullptr || names == nullptr) {
        return;
    }
    world.each<Shooter, const transform2d::Position>(
        [&](ecs::Entity, Shooter& shooter, const transform2d::Position& position) {
            if (shooter.wait > 0U) {
                --shooter.wait;
            }
            if (shooter.wait > 0U || shooter.action >= names->names.size() ||
                !actions->pressed(names->names[shooter.action])) {
                return;
            }
            shooter.wait = shooter.cooldown;
            requests->spawn_moving(shooter.prefab, position.x + shooter.dx, position.y + shooter.dy,
                                   shooter.dir_x * shooter.speed, shooter.dir_y * shooter.speed);
        });
}

} // namespace ge::mod::spawner2d
