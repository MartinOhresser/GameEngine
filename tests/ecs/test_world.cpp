#include "ge/ecs/ecs.hpp"
#include "ge/ecs/type_id.hpp"

#include "../support/mini_test.hpp"

#include <memory>
#include <string>
#include <utility>

using ge::ecs::Entity;
using ge::ecs::World;

namespace {

struct Position {
    int x;
    int y;
};

struct Velocity {
    int x;
    int y;
};

struct Tag {};

// Counts how many live values were destroyed, ignoring moved-from ones.
class Tracked {
public:
    explicit Tracked(int* destroyed) noexcept : destroyed_(destroyed) {}
    Tracked(const Tracked&) = delete;
    Tracked& operator=(const Tracked&) = delete;
    Tracked(Tracked&& other) noexcept : destroyed_(std::exchange(other.destroyed_, nullptr)) {}
    Tracked& operator=(Tracked&& other) noexcept {
        if (this != &other) {
            release();
            destroyed_ = std::exchange(other.destroyed_, nullptr);
        }
        return *this;
    }
    ~Tracked() { release(); }

private:
    void release() noexcept {
        if (destroyed_ != nullptr) {
            ++*destroyed_;
            destroyed_ = nullptr;
        }
    }

    int* destroyed_;
};

struct Gravity {
    float value;
};

} // namespace

TEST_CASE("world: spawn, insert, get and remove") {
    World world;
    const Entity entity = world.spawn();
    CHECK(world.insert(entity, Position{1, 2}));
    REQUIRE(world.get<Position>(entity) != nullptr);
    CHECK(world.get<Position>(entity)->y == 2);
    CHECK(world.has<Position>(entity));
    CHECK(world.remove<Position>(entity));
    CHECK(!world.has<Position>(entity));
    CHECK(!world.remove<Position>(entity));
}

TEST_CASE("world: queries on a component type nobody used return nothing") {
    struct Untouched {};
    World world;
    const Entity entity = world.spawn();
    CHECK(world.get<Untouched>(entity) == nullptr);
    CHECK(!world.has<Untouched>(entity));
    CHECK(world.count<Untouched>() == 0);
    CHECK(!world.remove<Untouched>(entity));
}

TEST_CASE("world: spawn_with attaches every component") {
    World world;
    const Entity entity = world.spawn_with(Position{1, 2}, Velocity{3, 4}, Tag{});
    CHECK(world.has<Position>(entity));
    CHECK(world.has<Velocity>(entity));
    CHECK(world.has<Tag>(entity));
    CHECK(world.count<Position>() == 1);
}

TEST_CASE("world: insert replaces the component of the same type") {
    World world;
    const Entity entity = world.spawn_with(Position{1, 1});
    CHECK(world.insert(entity, Position{9, 9}));
    REQUIRE(world.get<Position>(entity) != nullptr);
    CHECK(world.get<Position>(entity)->x == 9);
    CHECK(world.count<Position>() == 1);
}

TEST_CASE("world: emplace constructs in place and returns the component") {
    World world;
    const Entity entity = world.spawn();
    auto* text = world.emplace<std::string>(entity, std::size_t{3}, 'z');
    REQUIRE(text != nullptr);
    CHECK(*text == "zzz");
}

TEST_CASE("world: despawn removes every component and invalidates the handle") {
    World world;
    const Entity entity = world.spawn_with(Position{0, 0}, Velocity{1, 1}, Tag{});
    CHECK(world.despawn(entity));
    CHECK(!world.is_alive(entity));
    CHECK(!world.has<Position>(entity));
    CHECK(!world.has<Velocity>(entity));
    CHECK(world.count<Tag>() == 0);
    CHECK(!world.despawn(entity));
}

TEST_CASE("world: a stale handle does not see the new occupant of its slot") {
    World world;
    const Entity old_entity = world.spawn_with(Tag{});
    CHECK(world.despawn(old_entity));
    const Entity new_entity = world.spawn_with(Tag{});
    CHECK(old_entity.index() == new_entity.index());
    CHECK(!world.has<Tag>(old_entity));
    CHECK(world.has<Tag>(new_entity));
}

TEST_CASE("world: components cannot be added to a dead entity") {
    World world;
    const Entity entity = world.spawn();
    CHECK(world.despawn(entity));
    CHECK(!world.insert(entity, Tag{}));
    CHECK(world.emplace<Tag>(entity) == nullptr);
    CHECK(world.count<Tag>() == 0);
}

TEST_CASE("world: each visits every entity that has the component") {
    World world;
    for (int i = 0; i < 4; ++i) {
        world.spawn_with(Position{i, 0});
    }
    world.spawn_with(Tag{});
    int visited = 0;
    world.each<Position>([&visited](Entity, Position& position) {
        position.y = 7;
        ++visited;
    });
    CHECK(visited == 4);
    int sevens = 0;
    world.each<const Position>([&sevens](Entity, const Position& position) {
        if (position.y == 7) {
            ++sevens;
        }
    });
    CHECK(sevens == 4);
}

TEST_CASE("world: each over two components writes one and reads the other") {
    World world;
    const Entity both = world.spawn_with(Position{0, 0}, Velocity{2, 3});
    const Entity only_position = world.spawn_with(Position{5, 5});
    const Entity only_velocity = world.spawn_with(Velocity{1, 1});

    int visited = 0;
    world.each<Position, const Velocity>([&visited](Entity, Position& p, const Velocity& v) {
        p.x += v.x;
        p.y += v.y;
        ++visited;
    });

    CHECK(visited == 1);
    REQUIRE(world.get<Position>(both) != nullptr);
    CHECK(world.get<Position>(both)->x == 2);
    CHECK(world.get<Position>(both)->y == 3);
    CHECK(world.get<Position>(only_position)->x == 5);
    CHECK(!world.has<Position>(only_velocity));
}

TEST_CASE("world: each gives the same result whichever component is rarer") {
    World world;
    for (int i = 0; i < 10; ++i) {
        world.spawn_with(Position{i, 0});
    }
    const Entity moving = world.spawn_with(Position{100, 0}, Velocity{1, 0});

    int first_order = 0;
    world.each<Position, Velocity>([&first_order](Entity entity, Position&, Velocity&) {
        first_order += static_cast<int>(entity.index());
    });
    int second_order = 0;
    world.each<Velocity, Position>([&second_order](Entity entity, Velocity&, Position&) {
        second_order += static_cast<int>(entity.index());
    });

    CHECK(first_order == static_cast<int>(moving.index()));
    CHECK(second_order == first_order);
}

TEST_CASE("world: each hands the entity to the callback") {
    World world;
    const Entity entity = world.spawn_with(Tag{});
    Entity seen;
    world.each<Tag>([&seen](Entity visited, Tag&) { seen = visited; });
    CHECK(seen == entity);
}

TEST_CASE("world: each does nothing when a requested type has no storage") {
    struct Missing {};
    World world;
    world.spawn_with(Position{0, 0});
    int calls = 0;
    world.each<Position, Missing>([&calls](Entity, Position&, Missing&) { ++calls; });
    CHECK(calls == 0);
}

TEST_CASE("world: spawning from inside each is allowed") {
    World world;
    world.spawn_with(Tag{});
    world.each<Tag>([&world](Entity, Tag&) { static_cast<void>(world.spawn()); });
    CHECK(world.entity_count() == 2);
}

TEST_CASE("world: entity_count and entities track live entities") {
    World world;
    const Entity a = world.spawn();
    const Entity b = world.spawn();
    CHECK(world.despawn(a));
    CHECK(world.entity_count() == 1);
    const auto live = world.entities();
    REQUIRE(live.size() == 1);
    CHECK(live[0] == b);
}

TEST_CASE("world: a const world can be read and iterated") {
    World world;
    const Entity entity = world.spawn_with(Position{4, 5}, Velocity{1, 1});
    const World& view = world;
    REQUIRE(view.get<Position>(entity) != nullptr);
    CHECK(view.get<Position>(entity)->x == 4);
    CHECK(view.has<Velocity>(entity));
    CHECK(view.count<Position>() == 1);
    CHECK(view.get<Tag>(entity) == nullptr);

    int sum = 0;
    view.each<const Position, const Velocity>(
        [&sum](Entity, const Position& p, const Velocity& v) { sum += p.x + v.x; });
    CHECK(sum == 5);
}

TEST_CASE("world: components may be move-only") {
    World world;
    const Entity entity = world.spawn_with(std::make_unique<int>(41));
    auto* pointer = world.get<std::unique_ptr<int>>(entity);
    REQUIRE(pointer != nullptr);
    CHECK(**pointer == 41);
}

TEST_CASE("world: destroying, replacing and removing components destroys them") {
    int destroyed = 0;
    {
        World world;
        const Entity a = world.spawn_with(Tracked{&destroyed});
        CHECK(destroyed == 0);

        CHECK(world.insert(a, Tracked{&destroyed}));
        CHECK(destroyed == 1); // the replaced component

        CHECK(world.remove<Tracked>(a));
        CHECK(destroyed == 2);

        const Entity b = world.spawn_with(Tracked{&destroyed});
        CHECK(world.despawn(b));
        CHECK(destroyed == 3);

        static_cast<void>(world.spawn_with(Tracked{&destroyed}));
    }
    CHECK(destroyed == 4); // the world destroyed what it still held
}

TEST_CASE("world: resources are stored by type") {
    World world;
    CHECK(world.resource<Gravity>() == nullptr);

    world.insert_resource(Gravity{9.8F});
    const Gravity* first = world.resource<Gravity>();
    REQUIRE(first != nullptr);
    CHECK(first->value > 9.7F);

    world.insert_resource(Gravity{1.6F});
    const Gravity* second = world.resource<Gravity>();
    REQUIRE(second != nullptr);
    CHECK(second->value < 1.7F);

    const World& view = world;
    REQUIRE(view.resource<Gravity>() != nullptr);

    CHECK(world.remove_resource<Gravity>());
    CHECK(world.resource<Gravity>() == nullptr);
    CHECK(!world.remove_resource<Gravity>());
}

TEST_CASE("world: emplace_resource constructs in place") {
    World world;
    const auto& text = world.emplace_resource<std::string>(std::size_t{2}, 'q');
    CHECK(text.size() == 2);
    const std::string* stored = world.resource<std::string>();
    REQUIRE(stored != nullptr);
    CHECK(stored->size() == 2);
}

TEST_CASE("world: resources and components do not share ids") {
    World world;
    world.insert_resource(Position{1, 2});
    const Entity entity = world.spawn_with(Position{3, 4});
    const Position* as_resource = world.resource<Position>();
    const Position* as_component = world.get<Position>(entity);
    REQUIRE(as_resource != nullptr);
    REQUIRE(as_component != nullptr);
    CHECK(as_resource->x == 1);
    CHECK(as_component->x == 3);
}

TEST_CASE("world: a world can be moved") {
    World original;
    const Entity entity = original.spawn_with(Position{1, 1});
    original.insert_resource(Gravity{2.0F});

    World moved = std::move(original);
    CHECK(moved.is_alive(entity));
    CHECK(moved.has<Position>(entity));
    CHECK(moved.resource<Gravity>() != nullptr);

    World assigned;
    assigned = std::move(moved);
    CHECK(assigned.has<Position>(entity));
}

TEST_CASE("type ids: distinct per type and stable") {
    CHECK(ge::ecs::type_id<int>() != ge::ecs::type_id<float>());
    CHECK(ge::ecs::type_id<int>() == ge::ecs::type_id<int>());
}
