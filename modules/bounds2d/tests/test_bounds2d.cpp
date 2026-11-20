#include "ge/app/app.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/app/viewport.hpp"
#include "ge/mod/bounds2d/bounds2d.gen.hpp"
#include "ge/mod/bounds2d/edge.hpp"
#include "ge/mod/camera2d/camera2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

namespace {

namespace bounds2d = ge::mod::bounds2d;
namespace camera2d = ge::mod::camera2d;
namespace transform2d = ge::mod::transform2d;

using ge::ecs::Entity;

// A screen of 480 x 720 units, seen by a camera at (240, 360): it shows x 0 to 480, y 0 to 720.
struct Scene {
    ge::app::App app;
    Entity camera;

    Scene() {
        transform2d::install(app);
        camera2d::install(app);
        bounds2d::install(app);
        CHECK(app.module_problems().empty());
        CHECK(app.schedule().problems().empty());
        app.world().insert_resource(ge::app::Viewport{480, 720});
        app.world().insert_resource(ge::app::ActionNames{{"gone", "other"}});
        camera = app.world().spawn();
        CHECK(app.world().insert(camera, transform2d::Position{240.0F, 360.0F}));
        CHECK(app.world().insert(camera, camera2d::Camera{}));
    }

    Entity thing(float x, float y, bounds2d::OutOfView bounds) {
        const Entity entity = app.world().spawn();
        CHECK(app.world().insert(entity, transform2d::Position{x, y}));
        CHECK(app.world().insert(entity, bounds));
        return entity;
    }
};

bounds2d::OutOfView below(float margin) {
    bounds2d::OutOfView bounds; // the bottom edge by default
    bounds.margin = margin;
    return bounds;
}

} // namespace

TEST_CASE("bounds2d: what fell below the screen is removed, what is above stays") {
    Scene scene;
    const Entity inside = scene.thing(0.0F, 700.0F, below(100.0F));
    const Entity in_margin = scene.thing(0.0F, 800.0F, below(100.0F)); // 720 + 100 = 820
    const Entity gone = scene.thing(0.0F, 830.0F, below(100.0F));
    const Entity above = scene.thing(0.0F, -5000.0F, below(100.0F));
    scene.app.run_frames(1);
    CHECK(scene.app.world().is_alive(inside));
    CHECK(scene.app.world().is_alive(in_margin));
    CHECK(!scene.app.world().is_alive(gone));
    CHECK(scene.app.world().is_alive(above));
    scene.app.world().get<transform2d::Position>(scene.camera)->y = 60.0F; // the screen climbs
    scene.app.run_frames(1);
    CHECK(!scene.app.world().is_alive(inside));
}

TEST_CASE("bounds2d: the side of the screen is a setting") {
    Scene scene;
    bounds2d::OutOfView behind; // a runner leaves things on its left
    behind.side_x = -1.0F;
    behind.side_y = 0.0F;
    behind.margin = 100.0F;
    const Entity near_left = scene.thing(-50.0F, 300.0F, behind);
    const Entity far_left = scene.thing(-150.0F, 300.0F, behind);
    const Entity right = scene.thing(900.0F, 300.0F, behind);
    scene.app.run_frames(1);
    CHECK(scene.app.world().is_alive(near_left));
    CHECK(!scene.app.world().is_alive(far_left));
    CHECK(scene.app.world().is_alive(right));
}

TEST_CASE("bounds2d: it can announce without destroying, and announces once") {
    Scene scene;
    bounds2d::OutOfView bounds = below(10.0F);
    bounds.action = 0;
    bounds.despawn = false;
    const Entity fallen = scene.thing(0.0F, 900.0F, bounds);
    scene.app.run_frames(1);
    REQUIRE(scene.app.world().is_alive(fallen));
    CHECK(scene.app.world().get<bounds2d::OutOfView>(fallen)->fired);
    scene.app.run_frames(1);
    CHECK(scene.app.world().resource<ge::app::Actions>()->pressed("gone"));
    scene.app.run_frames(2);
    CHECK(!scene.app.world().resource<ge::app::Actions>()->pressed("gone"));
}

TEST_CASE("bounds2d: without a camera nothing is out of view") {
    ge::app::App app;
    transform2d::install(app);
    camera2d::install(app);
    bounds2d::install(app);
    app.world().insert_resource(ge::app::Viewport{480, 720});
    const Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{0.0F, 99999.0F}));
    CHECK(app.world().insert(entity, bounds2d::OutOfView{}));
    app.run_frames(2);
    CHECK(app.world().is_alive(entity));
}
