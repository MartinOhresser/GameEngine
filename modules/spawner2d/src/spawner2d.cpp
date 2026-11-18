#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/app/spawn.hpp"
#include "ge/app/viewport.hpp"
#include "ge/core/random.hpp"
#include "ge/mod/camera2d/camera2d.gen.hpp"
#include "ge/mod/spawner2d/spawner2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace ge::mod::spawner2d {

namespace {

/// What a prefab field holds when it names nothing.
constexpr std::uint32_t no_prefab = 0xFFFFFFFFU;

/// Most rows one generator builds in one tick: a camera that jumped far must not freeze the game.
constexpr int max_rows_per_tick = 64;

/// The part of the world the first camera shows, in y (which grows downwards).
struct View {
    bool found = false;
    float top = 0.0F;
    float bottom = 0.0F;
};

View view_of(ecs::World& world) {
    View view;
    const auto* viewport = world.resource<app::Viewport>();
    if (viewport == nullptr) {
        return view;
    }
    world.each<const camera2d::Camera, const transform2d::Position>(
        [&](ecs::Entity, const camera2d::Camera& camera, const transform2d::Position& position) {
            if (!view.found) {
                const float zoom = camera.zoom > 0.0F ? camera.zoom : 1.0F;
                const float half = static_cast<float>(viewport->height) / (2.0F * zoom);
                view = View{true, position.y - half, position.y + half};
            }
        });
    return view;
}

/// `first` at the bottom of the ramp, `last` at the top; a negative `last` means "same as first".
float ramped(float first, float last, float progress) {
    return last < 0.0F ? first : first + (last - first) * progress;
}

/// How far up the ramp a row at `height` is, from 0 to 1.
float progress_at(float height, float ramp_height) {
    if (ramp_height <= 0.0F) {
        return 1.0F;
    }
    return std::clamp(height / ramp_height, 0.0F, 1.0F);
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

/// Builds one row at `y`: its prefab and perhaps an extra one.
void build_row(const Generator& generator, float y, float progress, Random& random,
               app::SpawnRequests& requests) {
    const float x = between(random, generator.x_min, generator.x_max);
    const std::uint32_t prefab = pick_prefab(generator, progress, random);
    requests.spawn(prefab, x, y);
    if (generator.extra_prefab == no_prefab ||
        (!generator.extra_on_alt && prefab != generator.prefab)) {
        return;
    }
    const float chance = ramped(generator.extra_chance, generator.extra_chance_end, progress);
    if (random.chance(chance)) {
        const float shift = between(random, -generator.extra_dx, generator.extra_dx);
        requests.spawn(generator.extra_prefab, x + shift, y + generator.extra_dy);
    }
}

void run_generator(Generator& generator, float limit, Random& random,
                   app::SpawnRequests& requests) {
    for (int rows = 0; rows < max_rows_per_tick && generator.next_y > limit; ++rows) {
        const float y = generator.next_y;
        const float height = generator.origin_y - y;
        const float progress = progress_at(height, generator.ramp_height);
        const float low = ramped(generator.gap_min, generator.gap_min_end, progress);
        const float high = ramped(generator.gap_max, generator.gap_max_end, progress);
        generator.next_y -= std::max(between(random, low, high), 1.0F);
        const float chance = ramped(generator.row_chance, generator.row_chance_end, progress);
        if (height >= generator.from_height && random.chance(chance)) {
            build_row(generator, y, progress, random, requests);
        }
    }
}

} // namespace

void generate(ecs::World& world) {
    auto* requests = world.resource<app::SpawnRequests>();
    auto* random = world.resource<Random>();
    const View view = view_of(world);
    if (requests == nullptr || random == nullptr || !view.found) {
        return;
    }
    world.each<Generator>([&](ecs::Entity, Generator& generator) {
        run_generator(generator, view.top - generator.ahead, *random, *requests);
    });
}

void cull(ecs::World& world) {
    const View view = view_of(world);
    if (!view.found) {
        return;
    }
    std::vector<ecs::Entity> fallen;
    world.each<const Cull, const transform2d::Position>(
        [&](ecs::Entity entity, const Cull& cull, const transform2d::Position& position) {
            if (position.y > view.bottom + cull.margin) {
                fallen.push_back(entity);
            }
        });
    for (const ecs::Entity entity : fallen) {
        world.resource<ecs::Commands>()->despawn(entity);
    }
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
