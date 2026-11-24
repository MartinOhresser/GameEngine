#include "ge/app/app.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/mod/action_control/action_control.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

namespace {

namespace action_control = ge::mod::action_control;
namespace transform2d = ge::mod::transform2d;

ge::ecs::Entity spawn_controlled(ge::app::App& app, const action_control::Controlled& controlled) {
    const auto entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{}));
    CHECK(app.world().insert(entity, transform2d::Velocity{}));
    CHECK(app.world().insert(entity, controlled));
    return entity;
}

transform2d::Velocity velocity_of(ge::app::App& app, ge::ecs::Entity entity) {
    const auto* velocity = app.world().get<transform2d::Velocity>(entity);
    CHECK(velocity != nullptr);
    return velocity != nullptr ? *velocity : transform2d::Velocity{};
}

} // namespace

TEST_CASE("action_control: the actions set the velocity, an axis turned off is left alone") {
    ge::app::App app;
    transform2d::install(app);
    action_control::install(app);
    action_control::Controlled walker{50.0F};
    walker.vertical = false;
    const auto entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{}));
    CHECK(app.world().insert(entity, transform2d::Velocity{0.0F, 33.0F}));
    CHECK(app.world().insert(entity, walker));
    app.world().resource<ge::app::Actions>()->set("move_x", -1.0F);
    app.world().resource<ge::app::Actions>()->set("move_y", 1.0F);
    app.run_frames(1);
    const auto* velocity = app.world().get<transform2d::Velocity>(entity);
    REQUIRE(velocity != nullptr);
    CHECK(velocity->x == -50.0F);
    CHECK(velocity->y == 33.0F); // gravity or anything else keeps it
}

TEST_CASE("action_control: an entity that names its actions follows them, the others move_x") {
    ge::app::App app;
    transform2d::install(app);
    action_control::install(app);
    app.world().insert_resource(ge::app::ActionNames{{"p2_x", "p2_y"}});
    action_control::Controlled second{10.0F};
    second.action_x = 0;
    second.action_y = 1;
    const auto first_entity = spawn_controlled(app, action_control::Controlled{10.0F});
    const auto second_entity = spawn_controlled(app, second);
    auto* actions = app.world().resource<ge::app::Actions>();
    actions->set("move_x", 1.0F);
    actions->set("p2_x", -1.0F);
    actions->set("p2_y", 0.5F);
    app.run_frames(1);
    const transform2d::Velocity first_velocity = velocity_of(app, first_entity);
    const transform2d::Velocity second_velocity = velocity_of(app, second_entity);
    CHECK(first_velocity.x == 10.0F);
    CHECK(first_velocity.y == 0.0F);
    CHECK(second_velocity.x == -10.0F);
    CHECK(second_velocity.y == 5.0F);
}

TEST_CASE("action_control: with an acceleration the speed builds up, and braking slows it") {
    ge::app::App app;
    transform2d::install(app);
    action_control::install(app);
    app.set_tick_rate(10); // a tick lasts 0.1 s
    action_control::Controlled runner{100.0F};
    runner.vertical = false;
    runner.acceleration = 400.0F; // 40 per tick
    runner.braking = 1000.0F;     // 100 per tick
    const auto entity = spawn_controlled(app, runner);
    auto* actions = app.world().resource<ge::app::Actions>();
    actions->set("move_x", 1.0F);
    app.run_frames(1);
    CHECK(velocity_of(app, entity).x == 40.0F);
    app.run_frames(2);
    CHECK(velocity_of(app, entity).x == 100.0F); // 80, then the asked speed
    actions->set("move_x", -1.0F);
    app.run_frames(1);
    CHECK(velocity_of(app, entity).x == 0.0F); // turning around brakes
    app.run_frames(1);
    CHECK(velocity_of(app, entity).x == -40.0F); // then speeds up the other way
    actions->set("move_x", 0.0F);
    app.run_frames(1);
    CHECK(velocity_of(app, entity).x == 0.0F);
}
