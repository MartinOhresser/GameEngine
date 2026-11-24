#include "ge/app/app.hpp"
#include "ge/app/input.hpp"
#include "ge/app/viewport.hpp"
#include "ge/mod/aim2d/aim2d.gen.hpp"
#include "ge/mod/camera2d/camera2d.gen.hpp"
#include "ge/mod/tag/tag.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

#include <cmath>
#include <cstdint>

namespace {

namespace aim2d = ge::mod::aim2d;
namespace camera2d = ge::mod::camera2d;
namespace tag = ge::mod::tag;
namespace transform2d = ge::mod::transform2d;

constexpr std::uint32_t shooter = 0U;

ge::app::App game() {
    ge::app::App app;
    transform2d::install(app);
    camera2d::install(app);
    tag::install(app);
    aim2d::install(app);
    CHECK(app.module_problems().empty());
    CHECK(app.schedule().problems().empty());
    app.world().insert_resource(ge::app::Viewport{800U, 600U});
    return app;
}

/// Puts the pointer at a place of the window (an axis keeps its value from tick to tick).
void point(ge::app::App& app, float x, float y) {
    app.world().resource<ge::app::Actions>()->set("pointer_x", x);
    app.world().resource<ge::app::Actions>()->set("pointer_y", y);
}

bool near(float a, float b) { return std::abs(a - b) < 0.01F; }

} // namespace

TEST_CASE("aim2d: the aim is the world point under the pointer, through the camera") {
    ge::app::App app = game();
    const ge::ecs::Entity camera = app.world().spawn();
    CHECK(app.world().insert(camera, transform2d::Position{1000.0F, 500.0F}));
    CHECK(app.world().insert(camera, camera2d::Camera{2.0F}));
    const ge::ecs::Entity mark = app.world().spawn();
    CHECK(app.world().insert(mark, transform2d::Position{}));
    CHECK(app.world().insert(mark, aim2d::Aim{}));
    point(app, 600.0F, 300.0F); // 200 pixels right of the middle: 100 units at zoom 2
    app.run_frames(1);
    CHECK(near(app.world().get<transform2d::Position>(mark)->x, 1100.0F));
    CHECK(near(app.world().get<transform2d::Position>(mark)->y, 500.0F));
}

TEST_CASE("aim2d: within reach of who aims, and locked once it works") {
    ge::app::App app = game();
    const ge::ecs::Entity hero = app.world().spawn();
    CHECK(app.world().insert(hero, transform2d::Position{100.0F, 100.0F}));
    CHECK(app.world().insert(hero, tag::Tag{shooter}));
    aim2d::Aim reach;
    reach.from = shooter;
    reach.range = 50.0F;
    reach.lock = true;
    const ge::ecs::Entity mark = app.world().spawn();
    CHECK(app.world().insert(mark, transform2d::Position{}));
    CHECK(app.world().insert(mark, reach));
    CHECK(app.world().insert(mark, transform2d::Disabled{}));
    point(app, 400.0F, 100.0F); // no camera: 300 units to the right of the hero
    app.run_frames(1);
    const aim2d::Aim& aimed = *app.world().get<aim2d::Aim>(mark);
    CHECK(near(aimed.distance, 300.0F));
    CHECK(near(aimed.dir_x, 1.0F));
    CHECK(near(aimed.x, 400.0F));
    CHECK(near(app.world().get<transform2d::Position>(mark)->x, 150.0F)); // 50 units of reach
    CHECK(app.world().remove<transform2d::Disabled>(mark));
    point(app, 100.0F, 300.0F);
    app.run_frames(1);
    CHECK(near(app.world().get<transform2d::Position>(mark)->x, 150.0F)); // it stays
    CHECK(near(app.world().get<aim2d::Aim>(mark)->dir_y, 1.0F));
}
