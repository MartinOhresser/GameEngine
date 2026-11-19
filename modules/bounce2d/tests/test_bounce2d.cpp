#include "ge/app/app.hpp"
#include "ge/mod/bounce2d/bounce2d.gen.hpp"
#include "ge/mod/collision2d/collision2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

namespace {

namespace bounce2d = ge::mod::bounce2d;
namespace collision2d = ge::mod::collision2d;
namespace transform2d = ge::mod::transform2d;

using ge::ecs::Entity;

ge::app::App game() {
    ge::app::App app;
    transform2d::install(app);
    collision2d::install(app);
    bounce2d::install(app);
    CHECK(app.module_problems().empty());
    CHECK(app.schedule().problems().empty());
    app.set_tick_rate(10); // a tick lasts 0.1 s
    return app;
}

/// A body 20 wide and 20 high centred on (x, y), moving at (0, vy).
Entity spawn_jumper(ge::app::App& app, float x, float y, float vy) {
    const Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{x, y}));
    CHECK(app.world().insert(entity, transform2d::Velocity{0.0F, vy}));
    CHECK(app.world().insert(entity, collision2d::Collider{20.0F, 20.0F, 1, 0xFFFFFFFFU, false}));
    CHECK(app.world().insert(entity, bounce2d::Bouncer{}));
    return entity;
}

/// A surface 60 wide and 10 high whose top edge is at `top`.
Entity spawn_surface(ge::app::App& app, float x, float top, bounce2d::Bouncy bouncy) {
    const Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{x, top + 5.0F}));
    CHECK(app.world().insert(entity, collision2d::Collider{60.0F, 10.0F, 2, 0xFFFFFFFFU, false}));
    CHECK(app.world().insert(entity, bouncy));
    return entity;
}

bounce2d::Bouncy plain(float strength = 300.0F) {
    bounce2d::Bouncy bouncy;
    bouncy.strength = strength;
    return bouncy;
}

float y_of(ge::app::App& app, Entity entity) {
    return app.world().get<transform2d::Position>(entity)->y;
}

float vy_of(ge::app::App& app, Entity entity) {
    return app.world().get<transform2d::Velocity>(entity)->y;
}

} // namespace

TEST_CASE("bounce2d: a body that falls onto a surface leaves it with the surface's strength") {
    auto app = game();
    static_cast<void>(spawn_surface(app, 0.0F, 100.0F, plain(300.0F)));
    // bottom edge at 85, falling at 100: 0.1 s later it is at 95, then 105: it lands in 2 ticks
    const Entity jumper = spawn_jumper(app, 0.0F, 75.0F, 100.0F);
    app.run_frames(1);
    CHECK(vy_of(app, jumper) == 100.0F);
    app.run_frames(1);
    CHECK(vy_of(app, jumper) == -300.0F);
    CHECK(y_of(app, jumper) == 90.0F); // put on the surface: bottom edge 100
    CHECK(app.world().get<bounce2d::Bouncer>(jumper)->bounces == 1U);
}

TEST_CASE("bounce2d: a body that rises through a surface is not stopped") {
    auto app = game();
    static_cast<void>(spawn_surface(app, 0.0F, 100.0F, plain()));
    const Entity below = spawn_jumper(app, 0.0F, 125.0F, -100.0F);
    app.run_frames(5);
    CHECK(vy_of(app, below) == -100.0F);
    CHECK(y_of(app, below) < 100.0F); // it went up through the surface
    CHECK(app.world().get<bounce2d::Bouncer>(below)->bounces == 0U);
}

TEST_CASE("bounce2d: a body that only overlaps the surface's edge lands, one beside it does not") {
    auto app = game();
    static_cast<void>(spawn_surface(app, 0.0F, 100.0F, plain())); // spans -30..30
    const Entity edge = spawn_jumper(app, 35.0F, 75.0F, 100.0F);  // spans 25..45
    const Entity far = spawn_jumper(app, 60.0F, 75.0F, 100.0F);   // spans 50..70
    app.run_frames(2);
    CHECK(app.world().get<bounce2d::Bouncer>(edge)->bounces == 1U);
    CHECK(app.world().get<bounce2d::Bouncer>(far)->bounces == 0U);
    CHECK(vy_of(app, far) == 100.0F);
}

TEST_CASE("bounce2d: a fast fall does not go through a thin surface") {
    auto app = game();
    static_cast<void>(spawn_surface(app, 0.0F, 100.0F, plain(300.0F)));
    // 2000 units per second is 200 per tick: the body goes from above to far below in one tick
    const Entity bullet = spawn_jumper(app, 0.0F, 0.0F, 2000.0F);
    app.run_frames(1);
    CHECK(app.world().get<bounce2d::Bouncer>(bullet)->bounces == 1U);
    CHECK(vy_of(app, bullet) == -300.0F);
}

TEST_CASE("bounce2d: of two surfaces crossed at once the higher one is landed on") {
    auto app = game();
    static_cast<void>(spawn_surface(app, 0.0F, 120.0F, plain(111.0F)));
    static_cast<void>(spawn_surface(app, 0.0F, 100.0F, plain(222.0F)));
    const Entity jumper = spawn_jumper(app, 0.0F, 80.0F, 1000.0F); // bottom 90 -> 190
    app.run_frames(1);
    CHECK(vy_of(app, jumper) == -222.0F);
}

TEST_CASE("bounce2d: a body that starts below a surface is not thrown by it") {
    auto app = game();
    static_cast<void>(spawn_surface(app, 0.0F, 100.0F, plain()));
    const Entity jumper = spawn_jumper(app, 0.0F, 200.0F, 50.0F); // already under it, falling
    app.run_frames(5);
    CHECK(app.world().get<bounce2d::Bouncer>(jumper)->bounces == 0U);
    CHECK(vy_of(app, jumper) == 50.0F);
}

TEST_CASE("bounce2d: a vanishing surface bounces once and is gone") {
    auto app = game();
    bounce2d::Bouncy once = plain();
    once.vanish = true;
    const Entity cloud = spawn_surface(app, 0.0F, 100.0F, once);
    const Entity jumper = spawn_jumper(app, 0.0F, 85.0F, 100.0F);
    app.run_frames(1);
    CHECK(vy_of(app, jumper) == -300.0F);
    CHECK(!app.world().is_alive(cloud));
}

TEST_CASE("bounce2d: a surface that breaks gives no bounce and falls away") {
    auto app = game();
    bounce2d::Bouncy brittle = plain();
    brittle.breaks = true;
    brittle.fall_speed = 250.0F;
    const Entity plank = spawn_surface(app, 0.0F, 100.0F, brittle);
    const Entity jumper = spawn_jumper(app, 0.0F, 85.0F, 100.0F);
    app.run_frames(1);
    CHECK(vy_of(app, jumper) == 100.0F); // not thrown
    CHECK(app.world().get<bounce2d::Bouncer>(jumper)->bounces == 0U);
    REQUIRE(app.world().is_alive(plank));
    CHECK(app.world().get<bounce2d::Bouncy>(plank) == nullptr);
    CHECK(vy_of(app, plank) == 250.0F);
    // it is no longer a surface: the body falls through the place where it was
    app.run_frames(3);
    CHECK(app.world().get<bounce2d::Bouncer>(jumper)->bounces == 0U);
}

TEST_CASE("bounce2d: a surface that kills is removed by the landing") {
    auto app = game();
    bounce2d::Bouncy monster = plain(400.0F);
    monster.kills = true;
    const Entity target = spawn_surface(app, 0.0F, 100.0F, monster);
    const Entity jumper = spawn_jumper(app, 0.0F, 85.0F, 100.0F);
    app.run_frames(1);
    CHECK(vy_of(app, jumper) == -400.0F);
    CHECK(!app.world().is_alive(target));
}

TEST_CASE("bounce2d: a body thrown up keeps rising and bounces again on the way down") {
    auto app = game();
    static_cast<void>(spawn_surface(app, 0.0F, 100.0F, plain(300.0F)));
    const Entity jumper = spawn_jumper(app, 0.0F, 85.0F, 100.0F);
    app.run_frames(1); // lands
    CHECK(app.world().get<bounce2d::Bouncer>(jumper)->bounces == 1U);
    app.run_frames(2); // rises 30 per tick: it passes upwards through nothing
    CHECK(y_of(app, jumper) < 90.0F);
    CHECK(app.world().get<bounce2d::Bouncer>(jumper)->bounces == 1U);
}

TEST_CASE("bounce2d: a brittle plank over a solid surface breaks and the body still bounces") {
    auto app = game();
    bounce2d::Bouncy brittle = plain();
    brittle.breaks = true;
    const Entity plank = spawn_surface(app, 0.0F, 98.0F, brittle); // 2 units higher
    const Entity solid = spawn_surface(app, 0.0F, 100.0F, plain(300.0F));
    const Entity jumper = spawn_jumper(app, 0.0F, 85.0F, 100.0F);
    app.run_frames(1);
    CHECK(vy_of(app, jumper) == -300.0F);
    CHECK(app.world().get<bounce2d::Bouncer>(jumper)->bounces == 1U);
    CHECK(app.world().get<bounce2d::Bouncy>(plank) == nullptr); // it gave way
    CHECK(app.world().get<bounce2d::Bouncy>(solid) != nullptr);
}

TEST_CASE("bounce2d: a brittle plank under the surface the body bounces on is left alone") {
    auto app = game();
    bounce2d::Bouncy brittle = plain();
    brittle.breaks = true;
    const Entity solid = spawn_surface(app, 0.0F, 100.0F, plain(300.0F));
    const Entity plank = spawn_surface(app, 0.0F, 105.0F, brittle); // lower: not reached
    const Entity jumper = spawn_jumper(app, 0.0F, 85.0F, 100.0F);
    app.run_frames(1);
    CHECK(vy_of(app, jumper) == -300.0F);
    CHECK(app.world().get<bounce2d::Bouncy>(plank) != nullptr);
    CHECK(app.world().get<bounce2d::Bouncy>(solid) != nullptr);
}

namespace {

/// A body 20 by 20 at (x, y) that falls in the direction (fall_x, fall_y) at `speed`.
Entity spawn_faller(ge::app::App& app, float x, float y, float fall_x, float fall_y, float speed) {
    const Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{x, y}));
    CHECK(app.world().insert(entity, transform2d::Velocity{fall_x * speed, fall_y * speed}));
    CHECK(app.world().insert(entity, collision2d::Collider{20.0F, 20.0F, 1, 0xFFFFFFFFU, false}));
    bounce2d::Bouncer bouncer;
    bouncer.fall_x = fall_x;
    bouncer.fall_y = fall_y;
    CHECK(app.world().insert(entity, bouncer));
    return entity;
}

/// A wall 10 wide and 60 high centred on (x, y).
Entity spawn_wall(ge::app::App& app, float x, float y, bounce2d::Bouncy bouncy) {
    const Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{x, y}));
    CHECK(app.world().insert(entity, collision2d::Collider{10.0F, 60.0F, 2, 0xFFFFFFFFU, false}));
    CHECK(app.world().insert(entity, bouncy));
    return entity;
}

float x_of(ge::app::App& app, Entity entity) {
    return app.world().get<transform2d::Position>(entity)->x;
}

float vx_of(ge::app::App& app, Entity entity) {
    return app.world().get<transform2d::Velocity>(entity)->x;
}

} // namespace

TEST_CASE("bounce2d: a body that falls up lands on the underside of a surface") {
    auto app = game();
    // the surface fills y 95 to 105: its underside, where a body coming from below lands, is 105
    static_cast<void>(spawn_surface(app, 0.0F, 95.0F, plain(300.0F)));
    const Entity diver = spawn_faller(app, 0.0F, 135.0F, 0.0F, -1.0F, 100.0F);
    app.run_frames(1); // its top edge goes from 125 to 115
    CHECK(vy_of(app, diver) == -100.0F);
    app.run_frames(1); // then to 105: it reaches the underside
    CHECK(vy_of(app, diver) == 300.0F); // thrown back down, against its fall
    CHECK(y_of(app, diver) == 115.0F); // put on the underside: top edge 105
    CHECK(app.world().get<bounce2d::Bouncer>(diver)->bounces == 1U);
}

TEST_CASE("bounce2d: a body that falls to the right lands on the left side of a wall") {
    auto app = game();
    // the wall fills x 95 to 105 and y -30 to 30
    static_cast<void>(spawn_wall(app, 100.0F, 0.0F, plain(250.0F)));
    const Entity runner = spawn_faller(app, 65.0F, 0.0F, 1.0F, 0.0F, 100.0F);
    app.run_frames(1);
    CHECK(vx_of(app, runner) == 100.0F); // right edge at 85 then 95 on the next tick
    app.run_frames(1);
    CHECK(vx_of(app, runner) == -250.0F); // thrown back to the left
    CHECK(x_of(app, runner) == 85.0F); // put against the wall: right edge 95
}

TEST_CASE("bounce2d: a body that falls to the right ignores a wall behind it or above it") {
    auto app = game();
    static_cast<void>(spawn_wall(app, 100.0F, 0.0F, plain()));
    const Entity beside = spawn_faller(app, 65.0F, 200.0F, 1.0F, 0.0F, 100.0F); // misses it
    const Entity beyond = spawn_faller(app, 160.0F, 0.0F, 1.0F, 0.0F, 100.0F); // already past it
    app.run_frames(4);
    CHECK(vx_of(app, beside) == 100.0F);
    CHECK(vx_of(app, beyond) == 100.0F);
}

TEST_CASE("bounce2d: a plank that breaks falls the way the body falls") {
    auto app = game();
    bounce2d::Bouncy brittle = plain();
    brittle.breaks = true;
    brittle.fall_speed = 80.0F;
    const Entity wall = spawn_wall(app, 100.0F, 0.0F, brittle);
    static_cast<void>(spawn_faller(app, 65.0F, 0.0F, 1.0F, 0.0F, 100.0F));
    app.run_frames(2);
    CHECK(app.world().get<bounce2d::Bouncy>(wall) == nullptr);
    const auto* velocity = app.world().get<transform2d::Velocity>(wall);
    CHECK(velocity != nullptr);
    CHECK(velocity != nullptr && velocity->x == 80.0F);
    CHECK(velocity != nullptr && velocity->y == 0.0F);
}
