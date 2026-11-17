#include "ge/ecs/entity.hpp"
#include "ge/ecs/entity_allocator.hpp"

#include "../support/mini_test.hpp"

using ge::ecs::Entity;
using ge::ecs::EntityAllocator;

TEST_CASE("entity: the default handle is null and never alive") {
    const Entity null;
    const EntityAllocator allocator;
    CHECK(null.is_null());
    CHECK(!allocator.contains(null));
}

TEST_CASE("entity: handles compare by index and generation") {
    CHECK(Entity(1, 0) == Entity(1, 0));
    CHECK(Entity(1, 0) != Entity(2, 0));
    CHECK(Entity(1, 0) != Entity(1, 1));
}

TEST_CASE("entity: to_bits packs generation and index") {
    const Entity entity(7, 3);
    CHECK(entity.to_bits() == ((std::uint64_t{3} << 32U) | 7U));
}

TEST_CASE("allocator: hands out distinct live entities") {
    EntityAllocator allocator;
    const Entity a = allocator.allocate();
    const Entity b = allocator.allocate();
    CHECK(a != b);
    CHECK(allocator.contains(a));
    CHECK(allocator.contains(b));
    CHECK(allocator.size() == 2);
}

TEST_CASE("allocator: a recycled slot gets a new generation") {
    EntityAllocator allocator;
    const Entity first = allocator.allocate();
    CHECK(allocator.release(first));
    const Entity second = allocator.allocate();
    CHECK(first.index() == second.index());
    CHECK(first.generation() != second.generation());
    CHECK(!allocator.contains(first));
    CHECK(allocator.contains(second));
}

TEST_CASE("allocator: releasing a stale handle is a no-op") {
    EntityAllocator allocator;
    const Entity entity = allocator.allocate();
    CHECK(allocator.release(entity));
    CHECK(!allocator.release(entity));
    CHECK(allocator.size() == 0);
}

TEST_CASE("allocator: alive lists only live entities, by slot") {
    EntityAllocator allocator;
    const Entity a = allocator.allocate();
    const Entity b = allocator.allocate();
    const Entity c = allocator.allocate();
    CHECK(allocator.release(b));
    const auto alive = allocator.alive();
    REQUIRE(alive.size() == 2);
    CHECK(alive[0] == a);
    CHECK(alive[1] == c);
}
