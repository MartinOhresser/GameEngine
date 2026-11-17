// M1 exit criterion: the same seed and the same actions give the same state,
// and a copy of the world equals the original.

#include "ge/app/app.hpp"
#include "ge/core/random.hpp"

#include "../support/mini_test.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

using ge::Random;
using ge::app::App;
using ge::app::Order;
using ge::app::Stage;
using ge::app::Time;
using ge::ecs::Commands;
using ge::ecs::Entity;
using ge::ecs::Events;
using ge::ecs::World;

namespace {

struct Position {
    float x = 0.0F;
    float y = 0.0F;
};

struct Lifetime {
    std::int32_t ticks = 0;
};

struct Spawned {
    std::uint32_t index = 0;
};

// A small game-like simulation that uses everything M1 provides: the seeded
// generator, fixed ticks, deferred commands, events and system ordering.
void build_simulation(App& app, std::uint64_t seed) {
    app.insert_resource(Random{seed});
    app.add_event<Spawned>();
    app.add_system(
        Stage::FixedUpdate,
        [](World& world) {
            auto* random = world.resource<Random>();
            if (random->chance(0.5F)) {
                const Entity entity = world.resource<Commands>()->spawn_with(
                    world, Position{random->unit_float(), random->unit_float()},
                    Lifetime{random->range(2, 6)});
                world.resource<Events<Spawned>>()->send(Spawned{entity.index()});
            }
        },
        "spawn");
    app.add_system(
        Stage::FixedUpdate,
        [](World& world) {
            const float step = world.resource<Time>()->fixed_delta;
            world.each<Position, Lifetime>([step](Entity, Position& position, Lifetime& life) {
                position.x += step;
                --life.ticks;
            });
        },
        "move", Order::after({"spawn"}));
    app.add_system(
        Stage::FixedUpdate,
        [](World& world) {
            world.each<const Lifetime>([&world](Entity entity, const Lifetime& life) {
                if (life.ticks <= 0) {
                    world.resource<Commands>()->despawn(entity);
                }
            });
        },
        "expire", Order::after({"move"}));
}

std::vector<std::uint64_t> fingerprints_per_tick(std::uint64_t seed, std::uint32_t ticks) {
    App app;
    build_simulation(app, seed);
    std::vector<std::uint64_t> fingerprints;
    for (std::uint32_t tick = 0; tick < ticks; ++tick) {
        app.update();
        fingerprints.push_back(app.world().fingerprint());
    }
    return fingerprints;
}

} // namespace

TEST_CASE("determinism: the same seed gives the same state at every tick") {
    CHECK(fingerprints_per_tick(7, 120) == fingerprints_per_tick(7, 120));
}

TEST_CASE("determinism: another seed diverges") {
    CHECK(fingerprints_per_tick(7, 120) != fingerprints_per_tick(8, 120));
}

TEST_CASE("determinism: the simulation is not trivially empty") {
    App app;
    build_simulation(app, 7);
    app.run_frames(120);
    CHECK(app.world().entity_count() > 0U);
    CHECK(app.world().resource<Events<Spawned>>()->total_sent() > 10U);
    CHECK(app.world().fingerprint_is_complete() == false); // Commands is not hashable
}

TEST_CASE("determinism: frame timing does not change the simulation") {
    // 60 ticks fed as 60 frames of one tick, or as 30 frames of two ticks.
    App steady;
    build_simulation(steady, 3);
    App bursty;
    build_simulation(bursty, 3);
    for (int frame = 0; frame < 60; ++frame) {
        steady.update();
    }
    for (int frame = 0; frame < 30; ++frame) {
        bursty.advance(std::chrono::nanoseconds(2'000'000'000 / 60));
    }
    CHECK(steady.tick_count() == bursty.tick_count());
    CHECK(steady.world().fingerprint() == bursty.world().fingerprint());
}

TEST_CASE("determinism: a copy of the world equals the original, then they part ways") {
    App app;
    build_simulation(app, 11);
    app.run_frames(50);
    const std::uint64_t at_50 = app.world().fingerprint();

    auto snapshot = app.world().clone();
    REQUIRE(snapshot.has_value());
    CHECK(snapshot->fingerprint() == at_50);
    CHECK(snapshot->entity_count() == app.world().entity_count());

    app.run_frames(30);
    CHECK(app.world().fingerprint() != at_50);
    CHECK(snapshot->fingerprint() == at_50); // the copy did not move

    // A fresh run stopped at tick 50 lands on exactly the snapshot.
    App replay;
    build_simulation(replay, 11);
    replay.run_frames(50);
    CHECK(replay.world().fingerprint() == snapshot->fingerprint());
}
