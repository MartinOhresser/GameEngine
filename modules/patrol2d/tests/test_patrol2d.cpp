#include "ge/app/app.hpp"
#include "ge/mod/patrol2d/patrol2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

namespace {

namespace patrol2d = ge::mod::patrol2d;
namespace transform2d = ge::mod::transform2d;

ge::app::App game() {
    ge::app::App app;
    transform2d::install(app);
    patrol2d::install(app);
    CHECK(app.module_problems().empty());
    CHECK(app.schedule().problems().empty());
    app.set_tick_rate(10);
    return app;
}

ge::ecs::Entity spawn(ge::app::App& app, float x, float vx, patrol2d::Patrol patrol) {
    const auto entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{x, 0.0F}));
    CHECK(app.world().insert(entity, transform2d::Velocity{vx, 0.0F}));
    CHECK(app.world().insert(entity, patrol));
    return entity;
}

} // namespace

TEST_CASE("patrol2d: an entity goes back and forth between its limits") {
    auto app = game();
    patrol2d::Patrol path;
    path.min_x = 0.0F;
    path.max_x = 10.0F;
    const auto walker = spawn(app, 5.0F, 30.0F, path); // 3 units per tick
    app.run_frames(2);                                 // 8, 11 -> back on 10 and turned
    CHECK(app.world().get<transform2d::Position>(walker)->x == 10.0F);
    CHECK(app.world().get<transform2d::Velocity>(walker)->x == -30.0F);
    app.run_frames(4); // 7, 4, 1, -2 -> on 0 and turned
    CHECK(app.world().get<transform2d::Position>(walker)->x == 0.0F);
    CHECK(app.world().get<transform2d::Velocity>(walker)->x == 30.0F);
}

TEST_CASE("patrol2d: an entity that starts outside its path comes back to it") {
    auto app = game();
    patrol2d::Patrol path;
    path.min_x = 0.0F;
    path.max_x = 10.0F;
    const auto lost = spawn(app, 50.0F, 30.0F, path);
    app.run_frames(1);
    CHECK(app.world().get<transform2d::Position>(lost)->x == 10.0F);
    CHECK(app.world().get<transform2d::Velocity>(lost)->x == -30.0F);
}

TEST_CASE("patrol2d: an axis that is off is left alone") {
    auto app = game();
    patrol2d::Patrol path;
    path.patrol_x = false;
    path.max_x = 10.0F;
    const auto free_walker = spawn(app, 5.0F, 30.0F, path);
    app.run_frames(5);
    CHECK(app.world().get<transform2d::Position>(free_walker)->x == 20.0F);
}

TEST_CASE("patrol2d: a patrol can go up and down") {
    auto app = game();
    patrol2d::Patrol path;
    path.patrol_x = false;
    path.patrol_y = true;
    path.min_y = -5.0F;
    path.max_y = 5.0F;
    const auto lift = app.world().spawn();
    CHECK(app.world().insert(lift, transform2d::Position{}));
    CHECK(app.world().insert(lift, transform2d::Velocity{0.0F, 20.0F}));
    CHECK(app.world().insert(lift, path));
    app.run_frames(3); // 2, 4, 6 -> on 5 and turned
    CHECK(app.world().get<transform2d::Position>(lift)->y == 5.0F);
    CHECK(app.world().get<transform2d::Velocity>(lift)->y == -20.0F);
}
