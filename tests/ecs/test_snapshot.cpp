#include "ge/app/app.hpp"
#include "ge/core/random.hpp"
#include "ge/ecs/ecs.hpp"

#include "../support/mini_test.hpp"

#include <memory>

using ge::Random;
using ge::app::App;
using ge::ecs::Entity;
using ge::ecs::World;

namespace {

struct Position {
    float x = 0.0F;
    float y = 0.0F;
    void hash(ge::Hasher& hasher) const {
        hasher.add(x);
        hasher.add(y);
    }
};

struct Health {
    std::int32_t points = 0;
};

struct Opaque {
    std::shared_ptr<int> value; // copyable, but not Hashable
};

struct MoveOnly {
    std::unique_ptr<int> value;
};

} // namespace

TEST_CASE("snapshot: a clone equals the original and is independent") {
    World world;
    const Entity a = world.spawn_with(Position{1, 2}, Health{10});
    static_cast<void>(world.spawn_with(Health{5}));
    world.insert_resource(Random{42});

    auto copy = world.clone();
    REQUIRE(copy.has_value());
    CHECK(copy->fingerprint() == world.fingerprint());
    CHECK(copy->entity_count() == 2U);
    REQUIRE(copy->get<Position>(a) != nullptr);
    CHECK(copy->get<Position>(a)->x == 1.0F);
    CHECK(*copy->resource<Random>() == *world.resource<Random>());

    copy->get<Health>(a)->points = 99;
    CHECK(world.get<Health>(a)->points == 10);
    CHECK(copy->fingerprint() != world.fingerprint());
}

TEST_CASE("snapshot: a clone keeps handing out the same entities") {
    World world;
    const Entity a = world.spawn();
    static_cast<void>(world.spawn());
    CHECK(world.despawn(a));
    auto copy = world.clone();
    REQUIRE(copy.has_value());
    CHECK(copy->spawn() == world.spawn());
    CHECK(!copy->is_alive(a));
}

TEST_CASE("snapshot: a world with a move-only component cannot be cloned") {
    World world;
    static_cast<void>(world.spawn_with(MoveOnly{std::make_unique<int>(1)}));
    const auto copy = world.clone();
    CHECK(!copy.has_value());
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks): false positive on a moved unique_ptr
}

TEST_CASE("snapshot: move-only resources are left out of the copy") {
    World world;
    world.insert_resource(MoveOnly{std::make_unique<int>(1)});
    world.insert_resource(Health{3});
    auto copy = world.clone();
    REQUIRE(copy.has_value());
    CHECK(copy->resource<MoveOnly>() == nullptr);
    REQUIRE(copy->resource<Health>() != nullptr);
    CHECK(copy->resource<Health>()->points == 3);
}

TEST_CASE("fingerprint: sensitive to entities, components and resources") {
    World a;
    World b;
    CHECK(a.fingerprint() == b.fingerprint());
    const Entity ea = a.spawn_with(Position{1, 1});
    CHECK(a.fingerprint() != b.fingerprint());
    const Entity eb = b.spawn_with(Position{1, 1});
    CHECK(a.fingerprint() == b.fingerprint());
    a.get<Position>(ea)->y = 2.0F;
    CHECK(a.fingerprint() != b.fingerprint());
    b.get<Position>(eb)->y = 2.0F;
    CHECK(a.fingerprint() == b.fingerprint());
    a.insert_resource(Health{1});
    CHECK(a.fingerprint() != b.fingerprint());
    b.insert_resource(Health{1});
    CHECK(a.fingerprint() == b.fingerprint());
}

TEST_CASE("fingerprint: does not depend on storage order") {
    World a;
    World b;
    const Entity a1 = a.spawn_with(Health{1});
    const Entity a2 = a.spawn_with(Health{2});
    const Entity b1 = b.spawn_with(Health{1});
    const Entity b2 = b.spawn_with(Health{2});
    // Removing and re-adding changes the dense order, not the content.
    CHECK(a.remove<Health>(a1));
    CHECK(a.insert(a1, Health{1}));
    CHECK(a1 == b1);
    CHECK(a2 == b2);
    CHECK(a.fingerprint() == b.fingerprint());
}

TEST_CASE("fingerprint: knows when it does not cover every value") {
    World world;
    static_cast<void>(world.spawn_with(Position{1, 2}));
    CHECK(world.fingerprint_is_complete());
    static_cast<void>(world.spawn_with(Opaque{std::make_shared<int>(1)}));
    CHECK(!world.fingerprint_is_complete());

    World with_resource;
    with_resource.insert_resource(Opaque{});
    CHECK(!with_resource.fingerprint_is_complete());
}

TEST_CASE("fingerprint: Time ignores the real clock") {
    App first;
    App second;
    first.advance(std::chrono::milliseconds(10));
    second.advance(std::chrono::milliseconds(12));
    CHECK(first.world().fingerprint() == second.world().fingerprint());
}
