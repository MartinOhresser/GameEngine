#include "ge/app/app.hpp"
#include "ge/app/render.hpp"
#include "ge/mod/camera2d/camera2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

#include <memory>

namespace {

namespace camera2d = ge::mod::camera2d;
namespace transform2d = ge::mod::transform2d;

ge::app::App game(const std::shared_ptr<ge::app::RecordingRenderer>& renderer) {
    ge::app::App app;
    transform2d::install(app);
    camera2d::install(app);
    CHECK(app.module_problems().empty());
    CHECK(app.schedule().problems().empty());
    app.world().insert_resource(ge::app::RenderBackend{renderer});
    return app;
}

ge::ecs::Entity spawn_camera(ge::app::App& app, float x, float y, float zoom) {
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{x, y}));
    CHECK(app.world().insert(entity, camera2d::Camera{zoom}));
    return entity;
}

/// The camera the renderer was last given; a zoom of 0 if there was none.
ge::app::CameraView view_of(const ge::app::RecordingRenderer& renderer) {
    return renderer.camera().value_or(ge::app::CameraView{0.0F, 0.0F, 0.0F});
}

transform2d::Position position_of(ge::app::App& app, ge::ecs::Entity entity) {
    const auto* position = app.world().get<transform2d::Position>(entity);
    CHECK(position != nullptr);
    return position != nullptr ? *position : transform2d::Position{-1.0F, -1.0F};
}

} // namespace

TEST_CASE("camera2d: without a camera the renderer keeps its default view") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    app.run_frames(2);
    CHECK(!renderer->camera().has_value());
}

TEST_CASE("camera2d: the camera's position and zoom reach the renderer") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    spawn_camera(app, 120.0F, -30.0F, 2.5F);
    app.run_frames(1);
    CHECK(renderer->camera().has_value());
    const ge::app::CameraView view = view_of(*renderer);
    CHECK(view.x == 120.0F);
    CHECK(view.y == -30.0F);
    CHECK(view.zoom == 2.5F);
}

TEST_CASE("camera2d: with several cameras the oldest one is used") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    spawn_camera(app, 1.0F, 1.0F, 1.0F);
    spawn_camera(app, 9.0F, 9.0F, 9.0F);
    app.run_frames(1);
    CHECK(view_of(*renderer).x == 1.0F);
}

TEST_CASE("camera2d: a camera with stiffness 1 sticks to its target") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    const ge::ecs::Entity sticky = spawn_camera(app, 0.0F, 0.0F, 1.0F);
    CHECK(app.world().insert(sticky, camera2d::CameraFollow{1.0F}));
    const ge::ecs::Entity target = app.world().spawn();
    CHECK(app.world().insert(target, transform2d::Position{100.0F, 40.0F}));
    CHECK(app.world().insert(target, camera2d::CameraTarget{}));
    app.run_frames(1);
    CHECK(position_of(app, sticky).x == 100.0F);
    CHECK(position_of(app, sticky).y == 40.0F);
}

TEST_CASE("camera2d: a camera with a lower stiffness trails its target") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    const ge::ecs::Entity smooth = spawn_camera(app, 0.0F, 0.0F, 1.0F);
    CHECK(app.world().insert(smooth, camera2d::CameraFollow{0.5F}));
    const ge::ecs::Entity target = app.world().spawn();
    CHECK(app.world().insert(target, transform2d::Position{100.0F, 0.0F}));
    CHECK(app.world().insert(target, camera2d::CameraTarget{}));
    app.run_frames(1);
    CHECK(position_of(app, smooth).x == 50.0F);
    app.run_frames(1);
    CHECK(position_of(app, smooth).x == 75.0F);
}

TEST_CASE("camera2d: without a target, or with a stiffness of 0, the camera stays") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    const ge::ecs::Entity camera = spawn_camera(app, 5.0F, 5.0F, 1.0F);
    CHECK(app.world().insert(camera, camera2d::CameraFollow{1.0F}));
    app.run_frames(3); // no target yet
    CHECK(position_of(app, camera).x == 5.0F);
    const ge::ecs::Entity target = app.world().spawn();
    CHECK(app.world().insert(target, transform2d::Position{50.0F, 50.0F}));
    CHECK(app.world().insert(target, camera2d::CameraTarget{}));
    CHECK(app.world().insert(camera, camera2d::CameraFollow{0.0F}));
    app.run_frames(3);
    CHECK(position_of(app, camera).x == 5.0F);
}

namespace {

ge::ecs::Entity spawn_target(ge::app::App& app, float x, float y) {
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{x, y}));
    CHECK(app.world().insert(entity, camera2d::CameraTarget{}));
    return entity;
}

/// A camera at (`x`, `y`) that follows with the given settings.
ge::ecs::Entity spawn_follower(ge::app::App& app, float x, float y, camera2d::CameraFollow follow) {
    const ge::ecs::Entity camera = spawn_camera(app, x, y, 1.0F);
    CHECK(app.world().insert(camera, follow));
    return camera;
}

} // namespace

TEST_CASE("camera2d: an axis turned off is left alone") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    spawn_target(app, 300.0F, 500.0F);
    camera2d::CameraFollow follow;
    follow.follow_x = false;
    const auto camera = spawn_follower(app, 10.0F, 0.0F, follow);
    app.run_frames(1);
    CHECK(position_of(app, camera).x == 10.0F);
    CHECK(position_of(app, camera).y == 500.0F);
}

TEST_CASE("camera2d: offset_y shows the target above the middle of the screen") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    spawn_target(app, 0.0F, 400.0F);
    camera2d::CameraFollow follow;
    follow.offset_y = 120.0F;
    const auto camera = spawn_follower(app, 0.0F, 0.0F, follow);
    app.run_frames(1);
    CHECK(position_of(app, camera).y == 520.0F);
}

TEST_CASE("camera2d: only_up lets the camera climb but never come back down") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    const auto target = spawn_target(app, 0.0F, 0.0F);
    camera2d::CameraFollow follow;
    follow.only_up = true;
    const auto camera = spawn_follower(app, 0.0F, 0.0F, follow);

    app.world().get<transform2d::Position>(target)->y = -300.0F; // up: y shrinks
    app.run_frames(1);
    CHECK(position_of(app, camera).y == -300.0F);

    app.world().get<transform2d::Position>(target)->y = 250.0F; // falling back
    app.run_frames(3);
    CHECK(position_of(app, camera).y == -300.0F);

    app.world().get<transform2d::Position>(target)->y = -320.0F; // higher than ever
    app.run_frames(1);
    CHECK(position_of(app, camera).y == -320.0F);
}
