#include "ge/app/app.hpp"
#include "ge/mod/altitude/altitude.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

namespace {

namespace altitude = ge::mod::altitude;
namespace transform2d = ge::mod::transform2d;

ge::app::App game() {
    ge::app::App app;
    transform2d::install(app);
    altitude::install(app);
    CHECK(app.module_problems().empty());
    CHECK(app.schedule().problems().empty());
    app.set_tick_rate(10);
    return app;
}

} // namespace

TEST_CASE("altitude: the height is how far above its start the entity has been") {
    auto app = game();
    const auto climber = app.world().spawn();
    CHECK(app.world().insert(climber, transform2d::Position{0.0F, 500.0F}));
    CHECK(app.world().insert(climber, transform2d::Velocity{0.0F, -100.0F})); // 10 up per tick
    CHECK(app.world().insert(climber, altitude::Altitude{}));
    app.run_frames(5);
    const auto* measure = app.world().get<altitude::Altitude>(climber);
    REQUIRE(measure != nullptr);
    CHECK(measure->origin_y == 490.0F); // where it was at the end of its first tick
    CHECK(measure->height == 40.0F);
    CHECK(measure->score == 40);
}

TEST_CASE("altitude: falling back loses nothing") {
    auto app = game();
    const auto climber = app.world().spawn();
    CHECK(app.world().insert(climber, transform2d::Position{0.0F, 0.0F}));
    CHECK(app.world().insert(climber, altitude::Altitude{}));
    app.run_frames(1);
    app.world().get<transform2d::Position>(climber)->y = -120.0F;
    app.run_frames(1);
    app.world().get<transform2d::Position>(climber)->y = 300.0F;
    app.run_frames(3);
    CHECK(app.world().get<altitude::Altitude>(climber)->height == 120.0F);
}

TEST_CASE("altitude: the score is the height times the scale, rounded down") {
    auto app = game();
    const auto climber = app.world().spawn();
    altitude::Altitude measure;
    measure.scale = 0.1F;
    CHECK(app.world().insert(climber, transform2d::Position{0.0F, 0.0F}));
    CHECK(app.world().insert(climber, measure));
    app.run_frames(1);
    app.world().get<transform2d::Position>(climber)->y = -119.0F;
    app.run_frames(1);
    CHECK(app.world().get<altitude::Altitude>(climber)->score == 11); // 11.9
}
