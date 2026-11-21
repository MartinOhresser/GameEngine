#include "ge/app/app.hpp"
#include "ge/mod/progress/progress.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

namespace {

namespace progress = ge::mod::progress;
namespace transform2d = ge::mod::transform2d;

ge::app::App game() {
    ge::app::App app;
    transform2d::install(app);
    progress::install(app);
    CHECK(app.module_problems().empty());
    CHECK(app.schedule().problems().empty());
    app.set_tick_rate(10);
    return app;
}

} // namespace

TEST_CASE("progress: the distance is how far up from its start the entity has been") {
    auto app = game();
    const auto climber = app.world().spawn();
    CHECK(app.world().insert(climber, transform2d::Position{0.0F, 500.0F}));
    CHECK(app.world().insert(climber, transform2d::Velocity{0.0F, -100.0F})); // 10 up per tick
    CHECK(app.world().insert(climber, progress::Progress{}));
    app.run_frames(5);
    const auto* measure = app.world().get<progress::Progress>(climber);
    REQUIRE(measure != nullptr);
    CHECK(measure->origin == 490.0F); // where it was at the end of its first tick
    CHECK(measure->distance == 40.0F);
    CHECK(measure->score == 40);
}

TEST_CASE("progress: falling back loses nothing") {
    auto app = game();
    const auto climber = app.world().spawn();
    CHECK(app.world().insert(climber, transform2d::Position{0.0F, 0.0F}));
    CHECK(app.world().insert(climber, progress::Progress{}));
    app.run_frames(1);
    app.world().get<transform2d::Position>(climber)->y = -120.0F;
    app.run_frames(1);
    app.world().get<transform2d::Position>(climber)->y = 300.0F;
    app.run_frames(3);
    CHECK(app.world().get<progress::Progress>(climber)->distance == 120.0F);
}

TEST_CASE("progress: the score is the distance times the scale, rounded down") {
    auto app = game();
    const auto climber = app.world().spawn();
    progress::Progress measure;
    measure.scale = 0.1F;
    CHECK(app.world().insert(climber, transform2d::Position{0.0F, 0.0F}));
    CHECK(app.world().insert(climber, measure));
    app.run_frames(1);
    app.world().get<transform2d::Position>(climber)->y = -119.0F;
    app.run_frames(1);
    CHECK(app.world().get<progress::Progress>(climber)->score == 11); // 11.9
}

TEST_CASE("progress: toward the right it counts the distance run, and going back loses nothing") {
    auto app = game();
    const auto runner = app.world().spawn();
    progress::Progress measure;
    measure.toward_x = 1.0F;
    measure.toward_y = 0.0F;
    CHECK(app.world().insert(runner, transform2d::Position{40.0F, 300.0F}));
    CHECK(app.world().insert(runner, measure));
    app.run_frames(1);
    app.world().get<transform2d::Position>(runner)->x = 340.0F;
    app.world().get<transform2d::Position>(runner)->y = 9000.0F; // the other axis does not count
    app.run_frames(1);
    CHECK(app.world().get<progress::Progress>(runner)->distance == 300.0F);
    app.world().get<transform2d::Position>(runner)->x = 100.0F; // steps back
    app.run_frames(2);
    const auto* after = app.world().get<progress::Progress>(runner);
    CHECK(after->distance == 300.0F);
    CHECK(after->score == 300);
}

TEST_CASE("progress: toward the left or down it counts in that direction") {
    auto app = game();
    const auto left = app.world().spawn();
    progress::Progress toward_left;
    toward_left.toward_x = -1.0F;
    toward_left.toward_y = 0.0F;
    CHECK(app.world().insert(left, transform2d::Position{500.0F, 0.0F}));
    CHECK(app.world().insert(left, toward_left));
    const auto down = app.world().spawn();
    progress::Progress toward_down;
    toward_down.toward_x = 0.0F;
    toward_down.toward_y = 1.0F;
    CHECK(app.world().insert(down, transform2d::Position{0.0F, 20.0F}));
    CHECK(app.world().insert(down, toward_down));
    app.run_frames(1);
    app.world().get<transform2d::Position>(left)->x = 380.0F;
    app.world().get<transform2d::Position>(down)->y = 95.0F;
    app.run_frames(1);
    CHECK(app.world().get<progress::Progress>(left)->distance == 120.0F);
    CHECK(app.world().get<progress::Progress>(down)->distance == 75.0F);
}
