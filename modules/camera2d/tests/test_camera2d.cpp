#include "ge/app/app.hpp"
#include "ge/app/render.hpp"
#include "ge/app/viewport.hpp"
#include "ge/mod/camera2d/camera2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

#include <cmath>
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

TEST_CASE("camera2d: only_right lets the camera run to the right but never come back") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    const auto target = spawn_target(app, 0.0F, 0.0F);
    camera2d::CameraFollow follow;
    follow.follow_y = false;
    follow.only_right = true;
    const auto camera = spawn_follower(app, 0.0F, 7.0F, follow);

    app.world().get<transform2d::Position>(target)->x = 300.0F;
    app.run_frames(1);
    CHECK(position_of(app, camera).x == 300.0F);

    app.world().get<transform2d::Position>(target)->x = 100.0F; // the runner steps back
    app.run_frames(3);
    CHECK(position_of(app, camera).x == 300.0F);
    CHECK(position_of(app, camera).y == 7.0F); // follow_y is off

    app.world().get<transform2d::Position>(target)->x = 340.0F;
    app.run_frames(1);
    CHECK(position_of(app, camera).x == 340.0F);
}

TEST_CASE("camera2d: only_left and only_down are the mirror images of only_right and only_up") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    const auto target = spawn_target(app, 0.0F, 0.0F);
    camera2d::CameraFollow follow;
    follow.only_left = true;
    follow.only_down = true;
    const auto camera = spawn_follower(app, 0.0F, 0.0F, follow);

    app.world().get<transform2d::Position>(target)->x = -200.0F;
    app.world().get<transform2d::Position>(target)->y = 150.0F;
    app.run_frames(1);
    CHECK(position_of(app, camera).x == -200.0F);
    CHECK(position_of(app, camera).y == 150.0F);

    app.world().get<transform2d::Position>(target)->x = 50.0F; // back to the right
    app.world().get<transform2d::Position>(target)->y = 20.0F; // and up
    app.run_frames(3);
    CHECK(position_of(app, camera).x == -200.0F);
    CHECK(position_of(app, camera).y == 150.0F);
}

TEST_CASE("camera2d: offset_x puts the camera beside the target") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    static_cast<void>(spawn_target(app, 100.0F, 0.0F));
    camera2d::CameraFollow follow;
    follow.offset_x = -80.0F; // the camera is left of the runner: it sees what lies ahead
    const auto camera = spawn_follower(app, 0.0F, 0.0F, follow);
    app.run_frames(1);
    CHECK(position_of(app, camera).x == 20.0F);
}

namespace {

ge::ecs::Entity spawn_fit(ge::app::App& app, const camera2d::CameraFit& fit) {
    const ge::ecs::Entity entity = spawn_camera(app, 0.0F, 0.0F, 1.0F);
    CHECK(app.world().insert(entity, fit));
    return entity;
}

camera2d::CameraFit area(float width, float height) {
    camera2d::CameraFit fit;
    fit.width = width;
    fit.height = height;
    return fit;
}

} // namespace

TEST_CASE("camera2d: a fitted camera shows the whole area, whatever the shape of the window") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    app.world().resource<ge::app::Viewport>()->width = 400U;
    app.world().resource<ge::app::Viewport>()->height = 800U;
    camera2d::CameraFit fit = area(200.0F, 200.0F);
    fit.center_x = 100.0F;
    fit.center_y = 100.0F;
    const ge::ecs::Entity camera = spawn_fit(app, fit);
    app.run_frames(1);
    // A tall window: the width is the limit, so 400 pixels for 200 units.
    CHECK(view_of(*renderer).zoom == 2.0F);
    CHECK(position_of(app, camera).x == 100.0F);
    CHECK(position_of(app, camera).y == 100.0F);

    app.world().resource<ge::app::Viewport>()->width = 800U;
    app.world().resource<ge::app::Viewport>()->height = 400U;
    app.run_frames(1);
    CHECK(view_of(*renderer).zoom == 2.0F); // a wide window: now the height is
}

TEST_CASE("camera2d: margins leave room for an interface and max_zoom keeps a small area small") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    app.world().resource<ge::app::Viewport>()->width = 500U;
    app.world().resource<ge::app::Viewport>()->height = 700U;
    camera2d::CameraFit fit = area(100.0F, 100.0F);
    fit.margin_top = 100.0F;
    fit.margin_bottom = 0.0F;
    const ge::ecs::Entity camera = spawn_fit(app, fit);
    app.run_frames(1);
    // 500 wide and 600 high are free: the width limits, zoom 5; the area is centred in the free
    // part, whose middle is 50 pixels below the middle of the window, so the camera is 10 above.
    CHECK(view_of(*renderer).zoom == 5.0F);
    CHECK(position_of(app, camera).y == -10.0F);

    app.world().get<camera2d::CameraFit>(camera)->max_zoom = 3.0F;
    app.run_frames(1);
    CHECK(view_of(*renderer).zoom == 3.0F);
}

TEST_CASE("camera2d: within the dead zone the camera stays, past it it follows the excess") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    const auto target = spawn_target(app, 30.0F, -20.0F);
    camera2d::CameraFollow follow;
    follow.dead_x = 50.0F;
    follow.dead_y = 40.0F;
    const auto camera = spawn_follower(app, 0.0F, 0.0F, follow);
    app.run_frames(1);
    CHECK(position_of(app, camera).x == 0.0F);
    CHECK(position_of(app, camera).y == 0.0F);
    app.world().get<transform2d::Position>(target)->x = 80.0F;
    app.world().get<transform2d::Position>(target)->y = -100.0F;
    app.run_frames(1);
    CHECK(position_of(app, camera).x == 30.0F);  // the target is back on the zone's edge
    CHECK(position_of(app, camera).y == -60.0F); // the same upwards
}

TEST_CASE("camera2d: look_ahead aims ahead of a moving target, on the side it goes") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    const auto target = spawn_target(app, 100.0F, 0.0F);
    camera2d::CameraFollow follow;
    follow.look_ahead = 60.0F;
    const auto camera = spawn_follower(app, 0.0F, 0.0F, follow);
    app.run_frames(1);
    CHECK(position_of(app, camera).x == 100.0F); // no Velocity: standing still
    CHECK(app.world().insert(target, transform2d::Velocity{-200.0F, 0.0F}));
    app.run_frames(1);
    // It moved by 200 / 60 to the left, and the camera aims 60 to the left of it.
    const float x = app.world().get<transform2d::Position>(target)->x;
    CHECK(std::abs(position_of(app, camera).x - (x - 60.0F)) < 0.001F);
}

TEST_CASE("camera2d: a bounded camera never shows past the edges of the area") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    app.world().insert_resource(ge::app::Viewport{400, 200});
    const auto target = spawn_target(app, 0.0F, 0.0F);
    camera2d::CameraFollow follow;
    follow.bounded = true;
    follow.bound_left = -100.0F;
    follow.bound_top = -1000.0F;
    follow.bound_right = 2000.0F;
    follow.bound_bottom = 50.0F;
    const auto camera = spawn_follower(app, 0.0F, 0.0F, follow);
    app.run_frames(1);
    // The view is 400 by 200: its centre stays 200 from the left edge, 100 from the bottom.
    CHECK(position_of(app, camera).x == 100.0F);
    CHECK(position_of(app, camera).y == -50.0F);
    app.world().get<camera2d::Camera>(camera)->zoom = 2.0F; // a view of 200 by 100
    app.run_frames(1);
    CHECK(position_of(app, camera).x == 0.0F);
    CHECK(position_of(app, camera).y == 0.0F);
    app.world().get<transform2d::Position>(target)->x = 5000.0F;
    app.run_frames(1);
    CHECK(position_of(app, camera).x == 1900.0F);

    // An area narrower than the view: centred on it.
    follow.bound_right = 100.0F;
    *app.world().get<camera2d::CameraFollow>(camera) = follow;
    app.run_frames(1);
    CHECK(position_of(app, camera).x == 0.0F);
}

namespace {

/// A camera at (100, 50) shaken by `strength`, settling by 40 a second, at 10 ticks a second.
ge::ecs::Entity spawn_shaken(ge::app::App& app, float strength) {
    app.set_tick_rate(10); // a tick lasts 0.1 s
    const auto camera = spawn_camera(app, 100.0F, 50.0F, 1.0F);
    CHECK(app.world().insert(camera, camera2d::CameraShake{strength, 40.0F, 7.0F}));
    return camera;
}

} // namespace

TEST_CASE("camera2d: a shaken camera moves the picture by up to its strength") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    const auto camera = spawn_shaken(app, 8.0F);
    app.run_frames(1); // strength 4 after this tick
    const ge::app::CameraView view = view_of(*renderer);
    CHECK(std::abs(view.x - 100.0F) <= 4.0F);
    CHECK(std::abs(view.y - 50.0F) <= 4.0F);
    CHECK(view.x != 100.0F);
    CHECK(position_of(app, camera).x == 100.0F); // only the picture moves
}

TEST_CASE("camera2d: a shaken camera settles, then shows still") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    const auto camera = spawn_shaken(app, 8.0F);
    app.run_frames(2);
    CHECK(std::abs(app.world().get<camera2d::CameraShake>(camera)->strength) < 0.001F);
    app.run_frames(1);
    CHECK(view_of(*renderer).x == 100.0F);
    CHECK(view_of(*renderer).y == 50.0F);
}
