#include "ge/app/app.hpp"
#include "ge/mod/collision2d/collision2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

#include <cstdint>

namespace {

namespace collision2d = ge::mod::collision2d;
namespace transform2d = ge::mod::transform2d;

ge::app::App game() {
    ge::app::App app;
    transform2d::install(app);
    collision2d::install(app);
    CHECK(app.module_problems().empty());
    CHECK(app.schedule().problems().empty());
    return app;
}

ge::ecs::Entity box(ge::app::App& app, float x, float y, float width, float height) {
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{x, y}));
    CHECK(app.world().insert(entity, collision2d::Collider{width, height, 1U, 0xFFFFFFFFU, false}));
    return entity;
}

ge::ecs::Entity wall(ge::app::App& app, float x, float y, float width, float height) {
    const ge::ecs::Entity entity = box(app, x, y, width, height);
    app.world().get<collision2d::Collider>(entity)->solid = true;
    return entity;
}

ge::ecs::Entity mover(ge::app::App& app, float x, float y, float vx, float vy) {
    const ge::ecs::Entity entity = box(app, x, y, 10.0F, 10.0F);
    CHECK(app.world().insert(entity, collision2d::Body{}));
    CHECK(app.world().insert(entity, transform2d::Velocity{vx, vy}));
    return entity;
}

transform2d::Position position_of(ge::app::App& app, ge::ecs::Entity entity) {
    const auto* position = app.world().get<transform2d::Position>(entity);
    CHECK(position != nullptr);
    return position != nullptr ? *position : transform2d::Position{};
}

std::uint32_t count_of(ge::app::App& app, ge::ecs::Entity entity) {
    const auto* contacts = app.world().get<collision2d::Contacts>(entity);
    CHECK(contacts != nullptr);
    return contacts != nullptr ? contacts->count : 0xFFFFFFFFU;
}

} // namespace

TEST_CASE("collision2d: a body stops against a wall and loses the speed that went into it") {
    ge::app::App app = game();
    app.set_tick_rate(1); // one tick is one second: velocity is the step
    wall(app, 100.0F, 0.0F, 20.0F, 100.0F); // spans x 90..110
    const ge::ecs::Entity body = mover(app, 70.0F, 0.0F, 20.0F, 3.0F);
    app.run_frames(1); // moves to x 90, its right edge at 95: inside the wall
    const transform2d::Position position = position_of(app, body);
    CHECK(position.x == 85.0F); // its right edge rests on the wall's left edge
    CHECK(position.y == 3.0F);  // sliding along the wall is free
    const auto* velocity = app.world().get<transform2d::Velocity>(body);
    REQUIRE(velocity != nullptr);
    CHECK(velocity->x == 0.0F);
    CHECK(velocity->y == 3.0F);
}

TEST_CASE("collision2d: bodies are pushed out of floors, ceilings and right-hand walls too") {
    ge::app::App app = game();
    app.set_tick_rate(1);
    wall(app, 0.0F, 100.0F, 200.0F, 20.0F); // a floor: y 90..110
    const ge::ecs::Entity falling = mover(app, 0.0F, 80.0F, 0.0F, 8.0F);
    app.run_frames(1); // y 88, bottom edge 93: 3 into the floor
    CHECK(position_of(app, falling).y == 85.0F);
    CHECK(app.world().get<transform2d::Velocity>(falling)->y == 0.0F);

    ge::app::App other = game();
    other.set_tick_rate(1);
    wall(other, 100.0F, 0.0F, 20.0F, 100.0F);
    const ge::ecs::Entity from_right = mover(other, 130.0F, 0.0F, -25.0F, 0.0F);
    other.run_frames(1); // x 105, left edge 100: 10 into the wall (right face 110)
    CHECK(position_of(other, from_right).x == 115.0F);
}

TEST_CASE("collision2d: only bodies are stopped, and only by layers in their mask") {
    ge::app::App app = game();
    app.set_tick_rate(1);
    wall(app, 100.0F, 0.0F, 20.0F, 100.0F);
    const ge::ecs::Entity ghost = box(app, 95.0F, 0.0F, 10.0F, 10.0F); // no Body
    CHECK(app.world().insert(ghost, transform2d::Velocity{0.0F, 0.0F}));
    const ge::ecs::Entity ignoring = mover(app, 95.0F, 50.0F, 0.0F, 0.0F);
    app.world().get<collision2d::Collider>(ignoring)->mask = 2U; // wall is layer 1
    app.run_frames(1);
    CHECK(position_of(app, ghost).x == 95.0F);
    CHECK(position_of(app, ignoring).x == 95.0F);
}

TEST_CASE("collision2d: a body between two walls ends up in a valid place, every run") {
    const auto run = [] {
        ge::app::App app = game();
        app.set_tick_rate(1);
        wall(app, 100.0F, 0.0F, 20.0F, 100.0F);
        wall(app, 100.0F, 20.0F, 20.0F, 100.0F); // overlapping walls
        const ge::ecs::Entity body = mover(app, 70.0F, 10.0F, 20.0F, 0.0F);
        app.run_frames(5);
        CHECK(position_of(app, body).x <= 85.0F);
        return app.world().fingerprint();
    };
    CHECK(run() == run());
}

TEST_CASE("collision2d: contacts count the overlapping colliders and their layers") {
    ge::app::App app = game();
    const ge::ecs::Entity player = box(app, 0.0F, 0.0F, 10.0F, 10.0F);
    CHECK(app.world().insert(player, collision2d::Contacts{}));
    const ge::ecs::Entity coin = box(app, 8.0F, 0.0F, 10.0F, 10.0F); // overlaps by 2
    app.world().get<collision2d::Collider>(coin)->layer = 2U;
    const ge::ecs::Entity spikes = box(app, 0.0F, 9.0F, 10.0F, 10.0F); // overlaps by 1
    app.world().get<collision2d::Collider>(spikes)->layer = 4U;
    box(app, 100.0F, 100.0F, 10.0F, 10.0F); // far away
    app.run_frames(1);
    CHECK(count_of(app, player) == 2U); // the coin and the spikes, not the far box
    const auto* contacts = app.world().get<collision2d::Contacts>(player);
    CHECK(contacts->layers == 6U);
}

TEST_CASE("collision2d: boxes that only share an edge do not touch") {
    ge::app::App app = game();
    const ge::ecs::Entity left = box(app, 0.0F, 0.0F, 10.0F, 10.0F);
    CHECK(app.world().insert(left, collision2d::Contacts{}));
    box(app, 10.0F, 0.0F, 10.0F, 10.0F); // x 5..15 against 5..-5: the edges meet at 5
    app.run_frames(1);
    CHECK(count_of(app, left) == 0U);
}

TEST_CASE("collision2d: the mask picks what an entity notices, one side at a time") {
    ge::app::App app = game();
    const ge::ecs::Entity picky = box(app, 0.0F, 0.0F, 10.0F, 10.0F);
    CHECK(app.world().insert(picky, collision2d::Contacts{}));
    app.world().get<collision2d::Collider>(picky)->mask = 2U;
    const ge::ecs::Entity other = box(app, 3.0F, 0.0F, 10.0F, 10.0F); // layer 1
    CHECK(app.world().insert(other, collision2d::Contacts{}));
    app.run_frames(1);
    CHECK(count_of(app, picky) == 0U); // it only reacts to layer 2
    CHECK(count_of(app, other) == 1U); // the other one reacts to everything
}

TEST_CASE("collision2d: contacts drop back to zero when the boxes separate") {
    ge::app::App app = game();
    app.set_tick_rate(1);
    const ge::ecs::Entity a = box(app, 0.0F, 0.0F, 10.0F, 10.0F);
    CHECK(app.world().insert(a, collision2d::Contacts{}));
    const ge::ecs::Entity b = box(app, 5.0F, 0.0F, 10.0F, 10.0F);
    CHECK(app.world().insert(b, transform2d::Velocity{20.0F, 0.0F}));
    app.run_frames(1);
    CHECK(count_of(app, a) == 0U); // b moved to x 25 before detection
    app.world().get<transform2d::Position>(b)->x = 0.0F;
    app.world().get<transform2d::Velocity>(b)->x = 0.0F;
    app.run_frames(1);
    CHECK(count_of(app, a) == 1U);
}
