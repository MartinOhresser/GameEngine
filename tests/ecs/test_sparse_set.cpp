#include "ge/ecs/sparse_set.hpp"

#include "../support/mini_test.hpp"

#include <memory>
#include <string>

using ge::ecs::Entity;
using ge::ecs::SparseSet;
using ge::ecs::StorageBase;

TEST_CASE("sparse set: emplace, get and replace") {
    SparseSet<int> set;
    const Entity entity{3, 0};
    CHECK(set.get(entity) == nullptr);

    set.emplace(entity, 10);
    REQUIRE(set.get(entity) != nullptr);
    CHECK(*set.get(entity) == 10);

    set.emplace(entity, 20);
    REQUIRE(set.get(entity) != nullptr);
    CHECK(*set.get(entity) == 20);
    CHECK(set.size() == 1);
}

TEST_CASE("sparse set: removal keeps the other entries reachable") {
    SparseSet<int> set;
    for (std::uint32_t i = 0; i < 5; ++i) {
        set.emplace(Entity{i, 0}, static_cast<int>(i) * 10);
    }
    CHECK(set.remove(Entity{1, 0}));
    CHECK(!set.remove(Entity{1, 0}));
    CHECK(set.get(Entity{1, 0}) == nullptr);
    for (const std::uint32_t i : {0U, 2U, 3U, 4U}) {
        const int* value = set.get(Entity{i, 0});
        REQUIRE(value != nullptr);
        CHECK(*value == static_cast<int>(i) * 10);
    }
    CHECK(set.size() == 4);
}

TEST_CASE("sparse set: a stale generation does not match") {
    SparseSet<int> set;
    set.emplace(Entity{0, 1}, 1);
    CHECK(set.get(Entity{0, 0}) == nullptr);
    CHECK(!set.remove(Entity{0, 0}));
    CHECK(set.get(Entity{0, 1}) != nullptr);
}

TEST_CASE("sparse set: removal by slot through the base interface") {
    SparseSet<char> set;
    set.emplace(Entity{2, 0}, 'x');
    StorageBase& base = set;
    base.remove_entity(9); // unknown slot: nothing happens
    CHECK(set.size() == 1);
    base.remove_entity(2);
    CHECK(set.size() == 0);
    base.remove_entity(2); // already gone
    CHECK(set.size() == 0);
}

TEST_CASE("sparse set: entities and values stay aligned after removals") {
    SparseSet<int> set;
    for (std::uint32_t i = 0; i < 6; ++i) {
        set.emplace(Entity{i, 0}, static_cast<int>(i) + 100);
    }
    CHECK(set.remove(Entity{0, 0}));
    CHECK(set.remove(Entity{3, 0}));
    REQUIRE(set.entities().size() == set.values().size());
    for (std::size_t i = 0; i < set.entities().size(); ++i) {
        const int expected = static_cast<int>(set.entities()[i].index()) + 100;
        CHECK(set.values()[i] == expected);
    }
}

TEST_CASE("sparse set: components may be move-only or own memory") {
    SparseSet<std::unique_ptr<int>> pointers;
    pointers.emplace(Entity{0, 0}, std::make_unique<int>(5));
    pointers.emplace(Entity{0, 0}, std::make_unique<int>(6));
    REQUIRE(pointers.get(Entity{0, 0}) != nullptr);
    CHECK(**pointers.get(Entity{0, 0}) == 6);

    SparseSet<std::string> strings;
    strings.emplace(Entity{1, 0}, "hello");
    strings.emplace(Entity{2, 0}, std::size_t{3}, 'a');
    REQUIRE(strings.get(Entity{2, 0}) != nullptr);
    CHECK(*strings.get(Entity{2, 0}) == "aaa");
    CHECK(strings.remove(Entity{1, 0}));
    REQUIRE(strings.get(Entity{2, 0}) != nullptr);
    CHECK(*strings.get(Entity{2, 0}) == "aaa");
}

TEST_CASE("sparse set: a far-away slot index works") {
    SparseSet<int> set;
    set.emplace(Entity{100000, 0}, 1);
    REQUIRE(set.get(Entity{100000, 0}) != nullptr);
    CHECK(set.size() == 1);
    CHECK(set.get(Entity{99999, 0}) == nullptr);
}

TEST_CASE("sparse set: const access") {
    SparseSet<int> set;
    set.emplace(Entity{4, 0}, 8);
    const SparseSet<int>& view = set;
    REQUIRE(view.get(Entity{4, 0}) != nullptr);
    CHECK(*view.get(Entity{4, 0}) == 8);
    CHECK(view.get(Entity{5, 0}) == nullptr);
    CHECK(view.values().size() == 1);
}
