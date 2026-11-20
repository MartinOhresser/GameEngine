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

namespace {

const ge::app::DrawCommand* find(const ge::app::RecordingRenderer& renderer, const char* what) {
    for (const ge::app::DrawCommand& command : renderer.commands()) {
        if (command.what == what) {
            return &command;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("debug_draw: a rectangle is drawn with its own width, height and outline") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    const ge::ecs::Entity frame = app.world().spawn();
    CHECK(app.world().insert(frame, transform2d::Position{10.0F, 20.0F}));
    debug_draw::Rect rect;
    rect.width = 64.0F;
    rect.height = 32.0F;
    rect.thickness = 2.0F;
    rect.red = 0U;
    rect.green = 255U;
    rect.blue = 0U;
    rect.alpha = 128U;
    CHECK(app.world().insert(frame, rect));
    app.run_frames(1);
    const ge::app::DrawCommand* drawn = find(*renderer, "rect");
    REQUIRE(drawn != nullptr);
    ge::app::DrawCommand expected{"rect", 10.0F, 20.0F, 0.0F, 0x00FF0080U};
    expected.width = 64.0F;
    expected.height = 32.0F;
    expected.thickness = 2.0F;
    CHECK(*drawn == expected);
}

TEST_CASE("debug_draw: a circle is drawn with its radius and ring thickness") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    const ge::ecs::Entity ring = app.world().spawn();
    CHECK(app.world().insert(ring, transform2d::Position{1.0F, 2.0F}));
    CHECK(app.world().insert(ring, debug_draw::Circle{40.0F, 3.0F, 255U, 255U, 255U, 255U, false}));
    app.run_frames(1);
    const ge::app::DrawCommand* drawn = find(*renderer, "circle");
    REQUIRE(drawn != nullptr);
    ge::app::DrawCommand expected{"circle", 1.0F, 2.0F, 40.0F, 0xFFFFFFFFU};
    expected.thickness = 3.0F;
    CHECK(*drawn == expected);
}

TEST_CASE("debug_draw: a line goes from the position to the position plus its offset") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    const ge::ecs::Entity beam = app.world().spawn();
    CHECK(app.world().insert(beam, transform2d::Position{5.0F, 5.0F}));
    CHECK(
        app.world().insert(beam, debug_draw::Line{30.0F, -10.0F, 4.0F, 255U, 0U, 0U, 255U, true}));
    app.run_frames(1);
    const ge::app::DrawCommand* drawn = find(*renderer, "line");
    REQUIRE(drawn != nullptr);
    ge::app::DrawCommand expected{"line", 5.0F, 5.0F, 0.0F, 0xFF0000FFU};
    expected.x2 = 35.0F;
    expected.y2 = -5.0F;
    expected.thickness = 4.0F;
    expected.screen = true;
    CHECK(*drawn == expected);
}

TEST_CASE("debug_draw: a hidden shape is not drawn, and is again once shown") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{}));
    CHECK(app.world().insert(entity, debug_draw::Square{}));
    CHECK(app.world().insert(entity, debug_draw::Rect{}));
    CHECK(app.world().insert(entity, transform2d::Hidden{}));
    app.run_frames(1);
    CHECK(renderer->commands().empty());
    CHECK(app.world().remove<transform2d::Hidden>(entity));
    app.run_frames(1);
    CHECK(renderer->commands().size() == 2U);
}
