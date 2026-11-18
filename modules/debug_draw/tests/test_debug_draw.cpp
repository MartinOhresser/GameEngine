#include "ge/app/app.hpp"
#include "ge/app/render.hpp"
#include "ge/mod/debug_draw/debug_draw.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

#include <memory>

namespace {

namespace debug_draw = ge::mod::debug_draw;
namespace transform2d = ge::mod::transform2d;

ge::app::App game(const std::shared_ptr<ge::app::RecordingRenderer>& renderer) {
    ge::app::App app;
    transform2d::install(app);
    debug_draw::install(app);
    CHECK(app.module_problems().empty());
    CHECK(app.schedule().problems().empty());
    app.world().insert_resource(ge::app::RenderBackend{renderer});
    return app;
}

} // namespace

TEST_CASE("debug_draw: every square is drawn at its position, once per frame") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{10.0F, 20.0F}));
    CHECK(app.world().insert(entity, debug_draw::Square{32.0F, 255U, 0U, 128U}));
    app.run_frames(2);
    REQUIRE(renderer->commands().size() == 2U);
    CHECK(renderer->frames() == 2U);
    const ge::app::DrawCommand expected{"square", 10.0F, 20.0F, 32.0F, 0xFF0080FFU};
    CHECK(renderer->commands()[0] == expected);
    CHECK(renderer->commands()[1] == expected);
}

TEST_CASE("debug_draw: entities without a position or without a square are not drawn") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    const ge::ecs::Entity no_position = app.world().spawn();
    CHECK(app.world().insert(no_position, debug_draw::Square{}));
    const ge::ecs::Entity no_square = app.world().spawn();
    CHECK(app.world().insert(no_square, transform2d::Position{}));
    app.run_frames(1);
    CHECK(renderer->commands().empty());
    CHECK(renderer->frames() == 1U); // the frame still ends: the screen is cleared
}

TEST_CASE("debug_draw: colours above 255 are clamped") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{}));
    CHECK(app.world().insert(entity, debug_draw::Square{8.0F, 1000U, 0U, 0U}));
    app.run_frames(1);
    REQUIRE(renderer->commands().size() == 1U);
    CHECK(renderer->commands()[0].color == 0xFF0000FFU);
}

TEST_CASE("debug_draw: without a renderer resource nothing happens") {
    ge::app::App app;
    transform2d::install(app);
    debug_draw::install(app);
    app.world().remove_resource<ge::app::RenderBackend>();
    app.run_frames(1);
    CHECK(app.tick_count() == 1U);
}
