#include "ge/ecs/ecs.hpp"

#include "../support/mini_test.hpp"

#include <memory>
#include <vector>

using ge::ecs::Commands;
using ge::ecs::Entity;
using ge::ecs::World;

namespace {

struct Health {
    int points = 0;
};

struct Tag {};

struct Unique {
    std::unique_ptr<int> value;
};

} // namespace

TEST_CASE("commands: nothing changes until applied") {
    World world;
    Commands commands;
    const Entity entity = world.spawn();
    commands.insert(entity, Health{3});
    CHECK(commands.size() == 1U);
    CHECK(!commands.empty());
    CHECK(world.get<Health>(entity) == nullptr);
    commands.apply(world);
    CHECK(commands.empty());
    REQUIRE(world.get<Health>(entity) != nullptr);
    CHECK(world.get<Health>(entity)->points == 3);
}

TEST_CASE("commands: remove and despawn") {
    World world;
    Commands commands;
    const Entity a = world.spawn_with(Health{1}, Tag{});
    const Entity b = world.spawn_with(Health{2});
    commands.remove<Tag>(a);
    commands.despawn(b);
    commands.apply(world);
    CHECK(world.get<Tag>(a) == nullptr);
    CHECK(world.get<Health>(a) != nullptr);
    CHECK(!world.is_alive(b));
}

TEST_CASE("commands: spawn_with returns a usable entity at once") {
    World world;
    Commands commands;
    const Entity entity = commands.spawn_with(world, Health{9}, Tag{});
    CHECK(world.is_alive(entity));
    CHECK(world.get<Health>(entity) == nullptr);
    commands.apply(world);
    REQUIRE(world.get<Health>(entity) != nullptr);
    CHECK(world.get<Health>(entity)->points == 9);
    CHECK(world.get<Tag>(entity) != nullptr);
}

TEST_CASE("commands: run in queue order") {
    World world;
    Commands commands;
    std::vector<int> order;
    commands.push([&order](World&) { order.push_back(1); });
    commands.push([&order](World&) { order.push_back(2); });
    commands.push([&order](World&) { order.push_back(3); });
    commands.apply(world);
    CHECK(order == (std::vector<int>{1, 2, 3}));
}

TEST_CASE("commands: a command may queue more, applied in the same call") {
    World world;
    Commands commands;
    std::vector<int> order;
    commands.push([&](World&) {
        order.push_back(1);
        commands.push([&order](World&) { order.push_back(3); });
    });
    commands.push([&order](World&) { order.push_back(2); });
    commands.apply(world);
    CHECK(order == (std::vector<int>{1, 2, 3}));
    CHECK(commands.empty());
}

TEST_CASE("commands: stale entities are skipped") {
    World world;
    Commands commands;
    const Entity entity = world.spawn();
    CHECK(world.despawn(entity));
    commands.insert(entity, Health{1});
    commands.remove<Health>(entity);
    commands.despawn(entity);
    commands.apply(world);
    CHECK(!world.is_alive(entity));
    CHECK(world.entity_count() == 0U);
}

TEST_CASE("commands: move-only components are supported") {
    World world;
    Commands commands;
    const Entity entity = world.spawn();
    commands.insert(entity, Unique{std::make_unique<int>(42)});
    commands.apply(world);
    REQUIRE(world.get<Unique>(entity) != nullptr);
    CHECK(*world.get<Unique>(entity)->value == 42);
}

TEST_CASE("commands: queueing while iterating is safe") {
    World world;
    Commands commands;
    static_cast<void>(world.spawn_with(Health{0}));
    static_cast<void>(world.spawn_with(Health{5}));
    static_cast<void>(world.spawn_with(Health{0}));
    world.each<const Health>([&commands](Entity entity, const Health& health) {
        if (health.points <= 0) {
            commands.despawn(entity);
        }
    });
    CHECK(world.entity_count() == 3U);
    commands.apply(world);
    CHECK(world.entity_count() == 1U);
}

TEST_CASE("commands: moved queue keeps its content") {
    World world;
    Commands first;
    const Entity entity = world.spawn();
    first.insert(entity, Tag{});
    Commands second = std::move(first);
    second.apply(world);
    CHECK(world.get<Tag>(entity) != nullptr);
}
