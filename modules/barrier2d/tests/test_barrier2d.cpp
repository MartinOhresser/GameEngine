#include "ge/app/app.hpp"
#include "ge/app/render.hpp"
#include "ge/mod/barrier2d/barrier2d.gen.hpp"
#include "ge/mod/collision2d/collision2d.gen.hpp"
#include "ge/mod/gravity2d/gravity2d.gen.hpp"
#include "ge/mod/reserve/reserve.gen.hpp"
#include "ge/mod/sprite2d/sprite2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

namespace barrier2d = ge::mod::barrier2d;
namespace collision2d = ge::mod::collision2d;
namespace gravity2d = ge::mod::gravity2d;
namespace reserve = ge::mod::reserve;
namespace sprite2d = ge::mod::sprite2d;
namespace transform2d = ge::mod::transform2d;

constexpr std::uint32_t carbon = 0U;

ge::app::App game() {
    ge::app::App app;
    transform2d::install(app);
    collision2d::install(app);
    gravity2d::install(app);
    reserve::install(app);
    sprite2d::install(app);
    barrier2d::install(app);
    CHECK(app.module_problems().empty());
    CHECK(app.schedule().problems().empty());
    return app;
}

/// A caster at (x, y), not working yet, and a pool of three barriers.
ge::ecs::Entity caster(ge::app::App& app, float x, float y, float per_unit = 0.0F) {
    barrier2d::Caster made;
    made.start_radius = 20.0F;
    made.grow = 60.0F;
    made.max_radius = 50.0F;
    made.kind = carbon;
    made.per_unit = per_unit;
    made.end_x = x;
    made.end_y = y;
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{x, y}));
    CHECK(app.world().insert(entity, made));
    CHECK(app.world().insert(entity, transform2d::Disabled{}));
    for (int index = 0; index < 3; index += 1) {
        const ge::ecs::Entity barrier = app.world().spawn();
        CHECK(app.world().insert(barrier, transform2d::Position{}));
        CHECK(app.world().insert(barrier, barrier2d::Barrier{}));
    }
    return entity;
}

/// Holds the caster for `ticks` ticks, then lets it go.
void hold(ge::app::App& app, ge::ecs::Entity entity, std::uint32_t ticks) {
    CHECK(app.world().remove<transform2d::Disabled>(entity));
    app.run_frames(ticks);
    CHECK(app.world().insert(entity, transform2d::Disabled{}));
    app.run_frames(1);
}

std::vector<const barrier2d::Barrier*> raised(ge::app::App& app) {
    std::vector<const barrier2d::Barrier*> up;
    app.world().each<const barrier2d::Barrier>(
        [&up](ge::ecs::Entity, const barrier2d::Barrier& barrier) {
            if (barrier.up) {
                up.push_back(&barrier);
            }
        });
    return up;
}

ge::ecs::Entity body(ge::app::App& app, float x, float y) {
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{x, y}));
    CHECK(app.world().insert(entity, transform2d::Velocity{}));
    CHECK(app.world().insert(entity, collision2d::Collider{10.0F, 10.0F}));
    CHECK(app.world().insert(entity, collision2d::Body{}));
    CHECK(app.world().insert(entity, collision2d::Blocked{}));
    CHECK(app.world().insert(entity, gravity2d::Gravity{0.0F, 600.0F}));
    return entity;
}

} // namespace

TEST_CASE("barrier2d: a bubble grows while the caster works and is raised when it stops") {
    ge::app::App app = game();
    const ge::ecs::Entity stock = app.world().spawn();
    CHECK(app.world().insert(stock, reserve::Reserve{carbon, 100.0F, 100.0F}));
    const ge::ecs::Entity hand = caster(app, 0.0F, 0.0F, 1.0F);
    hold(app, hand, 31); // from 20, a unit a tick for 30 ticks
    CHECK(raised(app).size() == 1U);
    CHECK(std::abs(raised(app)[0]->radius - 50.0F) < 0.01F);
    CHECK(raised(app)[0]->round);
    CHECK(std::abs(app.world().get<reserve::Reserve>(stock)->amount - 50.0F) < 0.05F);
    CHECK(app.world().get<barrier2d::Caster>(hand)->radius == 0.0F);
    hold(app, hand, 31); // 50 more: the 50 left pay for it
    hold(app, hand, 1); // 20 more: refused
    CHECK(raised(app).size() == 2U);
    CHECK(app.world().get<barrier2d::Caster>(hand)->refused == 1U);
}

TEST_CASE("barrier2d: a fourth barrier takes the place of the first; a drag raises a wall") {
    ge::app::App app = game();
    const ge::ecs::Entity hand = caster(app, 0.0F, 0.0F);
    hold(app, hand, 1);
    hold(app, hand, 1);
    hold(app, hand, 1);
    CHECK(raised(app).size() == 3U);
    app.world().get<barrier2d::Caster>(hand)->end_x = 100.0F; // dragged 100 units away
    hold(app, hand, 1);
    CHECK(raised(app).size() == 3U);
    std::uint32_t walls = 0U;
    std::uint32_t lowest = 1000U;
    for (const barrier2d::Barrier* barrier : raised(app)) {
        walls += barrier->round ? 0U : 1U;
        lowest = std::min(lowest, barrier->order);
    }
    CHECK(walls == 1U);
    CHECK(lowest == 2U); // the first one (order 1) gave way
}

TEST_CASE("barrier2d: bodies stand on a wall and on a bubble; inside a bubble, time stops") {
    ge::app::App app = game();
    const ge::ecs::Entity hand = caster(app, -100.0F, 100.0F);
    app.world().get<barrier2d::Caster>(hand)->end_x = 100.0F; // a wall from x -100 to 100, y 100
    hold(app, hand, 1);
    const ge::ecs::Entity falling = body(app, 0.0F, 0.0F);
    app.run_frames(90);
    // On the wall: its top is 6 above its line (12 thick), the body is 10 high.
    CHECK(std::abs(app.world().get<transform2d::Position>(falling)->y - 89.0F) < 0.5F);
    CHECK(app.world().get<collision2d::Blocked>(falling)->below);

    ge::app::App bubble = game();
    const ge::ecs::Entity round = caster(bubble, 0.0F, 100.0F);
    hold(bubble, round, 1); // a bubble of radius 20 at (0, 100)
    const ge::ecs::Entity above = body(bubble, 0.0F, 0.0F);
    const ge::ecs::Entity inside = body(bubble, 0.0F, 95.0F);
    bubble.world().get<transform2d::Velocity>(inside)->x = 30.0F;
    bubble.run_frames(90);
    // On top of the bubble: 20 above its middle, and half the body (5).
    CHECK(std::abs(bubble.world().get<transform2d::Position>(above)->y - 75.0F) < 0.5F);
    CHECK(bubble.world().get<collision2d::Blocked>(above)->below);
    // Frozen where it was, its speed kept for when the bubble goes.
    CHECK(std::abs(bubble.world().get<transform2d::Position>(inside)->x) < 1.0F);
    CHECK(std::abs(bubble.world().get<transform2d::Velocity>(inside)->x - 30.0F) < 0.01F);
}

TEST_CASE("barrier2d: a barrier springs up when raised, lets glints fly, then breathes") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game();
    app.world().insert_resource(ge::app::RenderBackend{renderer});
    const ge::ecs::Entity bubble = app.world().spawn();
    CHECK(app.world().insert(bubble, transform2d::Position{100.0F, 100.0F}));
    barrier2d::Barrier made;
    made.radius = 40.0F;
    made.atlas = 0U;
    made.appear = 0.5F;
    made.wobble = 0.05F;
    made.sparks = 6U;
    CHECK(app.world().insert(bubble, made));
    const auto drawn = [&](std::uint32_t ticks, std::size_t& glints) {
        app.run_frames(ticks - 1U);
        renderer->clear(); // what the last frame draws
        app.run_frames(1U);
        float width = 0.0F;
        glints = 0U;
        for (const ge::app::DrawCommand& command : renderer->commands()) {
            if (command.what == "sprite") {
                width = command.width;
            } else if (command.what == "glow") {
                glints += 1U;
            }
        }
        return width;
    };
    std::size_t glints = 0U;
    app.world().get<barrier2d::Barrier>(bubble)->up = true;
    const float first = drawn(2U, glints);
    CHECK(first < 40.0F); // small at first
    CHECK(glints == 6U);
    const float later = drawn(60U, glints); // a second after: about its size, no glints left
    CHECK(glints == 0U);
    CHECK(std::abs(later - 80.0F) < 80.0F * 0.08F);
    CHECK(app.world().get<barrier2d::Barrier>(bubble)->age > 0.9F);
    // It still stops what it stops from the first tick: only the drawing springs up.
}

TEST_CASE("barrier2d: held counts the seconds something stays inside a bubble") {
    ge::app::App app = game();
    const ge::ecs::Entity bubble = app.world().spawn();
    CHECK(app.world().insert(bubble, transform2d::Position{0.0F, 0.0F}));
    barrier2d::Barrier made;
    made.radius = 50.0F;
    made.holds = 4U; // the layer of the walker below
    CHECK(app.world().insert(bubble, made));
    const ge::ecs::Entity walker = app.world().spawn();
    CHECK(app.world().insert(walker, transform2d::Position{10.0F, 0.0F}));
    CHECK(app.world().insert(walker, collision2d::Collider{10.0F, 10.0F, 4U, 0U}));
    app.run_frames(60);
    CHECK(app.world().get<barrier2d::Barrier>(bubble)->held == 0.0F); // down: nothing counted
    app.world().get<barrier2d::Barrier>(bubble)->up = true;
    app.run_frames(60);
    CHECK(std::abs(app.world().get<barrier2d::Barrier>(bubble)->held - 1.0F) < 0.05F);
    app.world().get<transform2d::Position>(walker)->x = 80.0F; // out: back to 0
    app.run_frames(2);
    CHECK(app.world().get<barrier2d::Barrier>(bubble)->held == 0.0F);
}
