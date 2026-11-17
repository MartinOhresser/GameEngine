#include "ge/app/app.hpp"

#include "../support/mini_test.hpp"

#include <vector>

using ge::app::App;
using ge::app::Stage;
using ge::ecs::Commands;
using ge::ecs::Entity;
using ge::ecs::EventCursor;
using ge::ecs::Events;
using ge::ecs::World;

namespace {

struct Mark {};

struct Ping {
    int id = 0;
};

struct Seen {
    std::vector<int> per_tick;
};

} // namespace

TEST_CASE("app: commands are applied after each system") {
    App app;
    app.add_system(
        Stage::FixedUpdate,
        [](World& world) {
            const Entity entity = world.spawn();
            world.resource<Commands>()->insert(entity, Mark{});
        },
        "spawner");
    app.add_system(
        Stage::FixedUpdate,
        [](World& world) {
            std::size_t marked = 0;
            world.each<const Mark>([&marked](Entity, const Mark&) { ++marked; });
            world.insert_resource(marked);
        },
        "counter", ge::app::Order::after({"spawner"}));
    app.update();
    CHECK(*app.world().resource<std::size_t>() == 1U);
    app.update();
    CHECK(*app.world().resource<std::size_t>() == 2U);
}

TEST_CASE("app: despawning from inside each works through commands") {
    App app;
    static_cast<void>(app.world().spawn_with(Mark{}));
    static_cast<void>(app.world().spawn_with(Mark{}));
    app.add_system(Stage::Update, [](World& world) {
        world.each<const Mark>(
            [&world](Entity entity, const Mark&) { world.resource<Commands>()->despawn(entity); });
    });
    app.update();
    CHECK(app.world().entity_count() == 0U);
}

TEST_CASE("app: an event lives for the tick it was sent and the next one") {
    App app;
    app.add_event<Ping>();
    app.insert_resource(Seen{});
    // Sender: only on the first tick.
    app.add_system(
        Stage::FixedUpdate,
        [](World& world) {
            if (world.resource<ge::app::Time>()->tick == 0) {
                world.resource<Events<Ping>>()->send(Ping{1});
            }
        },
        "sender");
    // Reader without a cursor: counts events stored at the time it runs.
    app.add_system(
        Stage::FixedUpdate,
        [](World& world) {
            world.resource<Seen>()->per_tick.push_back(
                static_cast<int>(world.resource<Events<Ping>>()->size()));
        },
        "reader", ge::app::Order::after({"sender"}));
    app.run_frames(4);
    CHECK(app.world().resource<Seen>()->per_tick == (std::vector<int>{1, 1, 0, 0}));
}

TEST_CASE("app: an event emitted in Update is dropped per frame, not per tick") {
    App app;
    app.add_event<Ping>(Stage::Update);
    app.add_system(Stage::Update,
                   [](World& world) { world.resource<Events<Ping>>()->send(Ping{1}); });
    app.run_frames(5);
    CHECK(app.world().resource<Events<Ping>>()->size() == 1U);
    CHECK(app.world().resource<Events<Ping>>()->total_sent() == 5U);
}

TEST_CASE("app: a reader with a cursor sees every event exactly once") {
    App app;
    app.add_event<Ping>();
    app.insert_resource(Seen{});
    app.insert_resource(EventCursor{});
    app.add_system(Stage::FixedUpdate, [](World& world) {
        world.resource<Events<Ping>>()->send(
            Ping{static_cast<int>(world.resource<ge::app::Time>()->tick)});
    });
    app.add_system(Stage::FixedUpdate, [](World& world) {
        auto* seen = world.resource<Seen>();
        world.resource<Events<Ping>>()->read(
            *world.resource<EventCursor>(),
            [seen](const Ping& ping) { seen->per_tick.push_back(ping.id); });
    });
    app.run_frames(4);
    CHECK(app.world().resource<Seen>()->per_tick == (std::vector<int>{0, 1, 2, 3}));
}
