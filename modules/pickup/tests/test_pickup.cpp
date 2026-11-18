#include "ge/app/app.hpp"
#include "ge/mod/pickup/pickup.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

namespace {

using ge::ecs::Entity;
namespace pickup = ge::mod::pickup;
namespace transform2d = ge::mod::transform2d;

ge::app::App game() {
    ge::app::App app;
    transform2d::install(app);
    pickup::install(app);
    CHECK(app.module_problems().empty());
    return app;
}

Entity spawn_collector(ge::app::App& app, float x, float y, float radius) {
    const Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{x, y}));
    CHECK(app.world().insert(entity, pickup::Collector{radius, 0}));
    return entity;
}

Entity spawn_pickup(ge::app::App& app, float x, float y, int value) {
    const Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{x, y}));
    CHECK(app.world().insert(entity, pickup::Pickup{value, 8.0F}));
    return entity;
}

int score(const ge::app::App& app, Entity collector) {
    return app.world().get<pickup::Collector>(collector)->score;
}

} // namespace

TEST_CASE("pickup: touching a pickup takes its value and removes it") {
    auto app = game();
    const Entity ship = spawn_collector(app, 100.0F, 100.0F, 16.0F);
    const Entity coin = spawn_pickup(app, 110.0F, 100.0F, 5);
    app.run_frames(1);
    CHECK(score(app, ship) == 5);
    CHECK(!app.world().is_alive(coin));
    app.run_frames(5);
    CHECK(score(app, ship) == 5); // a pickup is only taken once
}

TEST_CASE("pickup: the reach is the sum of both radii, edges included") {
    auto app = game();
    const Entity ship = spawn_collector(app, 0.0F, 0.0F, 16.0F);
    const Entity just_in = spawn_pickup(app, 24.0F, 0.0F, 1);  // 16 + 8
    const Entity just_out = spawn_pickup(app, 0.0F, 24.5F, 1); // a little too far
    app.run_frames(1);
    CHECK(score(app, ship) == 1);
    CHECK(!app.world().is_alive(just_in));
    CHECK(app.world().is_alive(just_out));
}

TEST_CASE("pickup: a pickup touched by two collectors goes to the first one only") {
    auto app = game();
    const Entity first = spawn_collector(app, 0.0F, 0.0F, 16.0F);
    const Entity second = spawn_collector(app, 1.0F, 0.0F, 16.0F);
    static_cast<void>(spawn_pickup(app, 5.0F, 0.0F, 3));
    app.run_frames(1);
    CHECK(score(app, first) + score(app, second) == 3);
    CHECK(score(app, first) == 3);
}

TEST_CASE("pickup: several pickups in one tick add up, and entities without Position are ignored") {
    auto app = game();
    const Entity ship = spawn_collector(app, 0.0F, 0.0F, 16.0F);
    static_cast<void>(spawn_pickup(app, 5.0F, 0.0F, 2));
    static_cast<void>(spawn_pickup(app, -5.0F, 0.0F, 4));
    const Entity lost = app.world().spawn(); // a Pickup nobody can place
    CHECK(app.world().insert(lost, pickup::Pickup{100, 8.0F}));
    app.run_frames(1);
    CHECK(score(app, ship) == 6);
    CHECK(app.world().is_alive(lost));
}

TEST_CASE("pickup: a moving collector picks up what it reaches, after it moved") {
    auto app = game();
    const Entity ship = spawn_collector(app, 0.0F, 0.0F, 4.0F);
    CHECK(app.world().insert(ship, transform2d::Velocity{60.0F, 0.0F})); // 1 unit per tick
    static_cast<void>(spawn_pickup(app, 20.0F, 0.0F, 7));
    app.run_frames(7); // x = 7, reach 12: the coin at 20 is just out
    CHECK(score(app, ship) == 0);
    app.run_frames(1); // x = 8, reach 12: the coin at 20 is touched
    CHECK(score(app, ship) == 7);
}

TEST_CASE("pickup: nothing to collect, nothing happens") {
    auto app = game();
    const Entity ship = spawn_collector(app, 0.0F, 0.0F, 16.0F);
    app.run_frames(3);
    CHECK(score(app, ship) == 0);
}
