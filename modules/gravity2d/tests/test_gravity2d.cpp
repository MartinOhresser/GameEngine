#include "ge/app/app.hpp"
#include "ge/mod/gravity2d/gravity2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

namespace {

namespace gravity2d = ge::mod::gravity2d;
namespace transform2d = ge::mod::transform2d;

ge::app::App game() {
    ge::app::App app;
    transform2d::install(app);
    gravity2d::install(app);
    CHECK(app.module_problems().empty());
    CHECK(app.schedule().problems().empty());
    app.set_tick_rate(10); // a tick lasts 0.1 s
    return app;
}

ge::ecs::Entity spawn(ge::app::App& app, float vy, gravity2d::Gravity gravity) {
    const auto entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{}));
    CHECK(app.world().insert(entity, transform2d::Velocity{0.0F, vy}));
    CHECK(app.world().insert(entity, gravity));
    return entity;
}

float speed_y(ge::app::App& app, ge::ecs::Entity entity) {
    const auto* velocity = app.world().get<transform2d::Velocity>(entity);
    CHECK(velocity != nullptr);
    return velocity != nullptr ? velocity->y : 0.0F;
}

} // namespace

TEST_CASE("gravity2d: a body speeds up by gravity times the tick, and moves the same tick") {
    auto app = game();
    const auto ball = spawn(app, 0.0F, gravity2d::Gravity{0.0F, 100.0F, 0.0F});
    app.run_frames(1);
    CHECK(speed_y(app, ball) == 10.0F);
    CHECK(app.world().get<transform2d::Position>(ball)->y == 1.0F); // 10 * 0.1
    app.run_frames(1);
    CHECK(speed_y(app, ball) == 20.0F);
}

TEST_CASE("gravity2d: a jump slows down, stops at the top and comes back") {
    auto app = game();
    const auto jumper = spawn(app, -50.0F, gravity2d::Gravity{0.0F, 100.0F, 0.0F});
    app.run_frames(5);
    CHECK(speed_y(app, jumper) == 0.0F); // 5 ticks of 10: the top of the jump
    app.run_frames(2);
    CHECK(speed_y(app, jumper) == 20.0F);
}

TEST_CASE("gravity2d: the terminal speed limits the fall but not the way up") {
    auto app = game();
    const auto faller = spawn(app, 0.0F, gravity2d::Gravity{0.0F, 100.0F, 25.0F});
    app.run_frames(10);
    CHECK(speed_y(app, faller) == 25.0F);

    const auto rocket = spawn(app, -500.0F, gravity2d::Gravity{0.0F, 100.0F, 25.0F});
    app.run_frames(1);
    CHECK(speed_y(app, rocket) == -490.0F); // a fast jump is not cut short
}

TEST_CASE("gravity2d: the speed_scale of transform2d slows gravity down too") {
    auto app = game();
    app.world().resource<transform2d::Params>()->speed_scale = 0.5F;
    const auto ball = spawn(app, 0.0F, gravity2d::Gravity{0.0F, 100.0F, 0.0F});
    app.run_frames(1);
    CHECK(speed_y(app, ball) == 5.0F);
}

TEST_CASE("gravity2d: gravity may pull sideways") {
    auto app = game();
    const auto leaf = app.world().spawn();
    CHECK(app.world().insert(leaf, transform2d::Position{}));
    CHECK(app.world().insert(leaf, transform2d::Velocity{}));
    CHECK(app.world().insert(leaf, gravity2d::Gravity{-40.0F, 0.0F, 0.0F}));
    app.run_frames(2);
    CHECK(app.world().get<transform2d::Velocity>(leaf)->x == -8.0F);
    CHECK(app.world().get<transform2d::Velocity>(leaf)->y == 0.0F);
}
