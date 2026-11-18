#include "ge/app/app.hpp"
#include "ge/app/render.hpp"
#include "ge/mod/sprite2d/sprite2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

#include <memory>

namespace {

namespace sprite2d = ge::mod::sprite2d;
namespace transform2d = ge::mod::transform2d;

ge::app::App game(const std::shared_ptr<ge::app::RecordingRenderer>& renderer) {
    ge::app::App app;
    transform2d::install(app);
    sprite2d::install(app);
    CHECK(app.module_problems().empty());
    CHECK(app.schedule().problems().empty());
    app.world().insert_resource(ge::app::RenderBackend{renderer});
    return app;
}

ge::ecs::Entity spawn(ge::app::App& app, float x, float y, const sprite2d::Sprite& sprite) {
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{x, y}));
    CHECK(app.world().insert(entity, sprite));
    return entity;
}

} // namespace

TEST_CASE("sprite2d: a sprite is drawn from its atlas cell at its position") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    sprite2d::Sprite sprite;
    sprite.atlas = 2U;
    sprite.frame = 5U;
    sprite.width = 32.0F;
    sprite.height = 16.0F;
    sprite.flip_x = true;
    sprite.red = 255U;
    sprite.green = 128U;
    sprite.blue = 0U;
    sprite.alpha = 200U;
    spawn(app, 10.0F, 20.0F, sprite);
    app.run_frames(1);
    REQUIRE(renderer->commands().size() == 1U);
    const ge::app::DrawCommand& command = renderer->commands()[0];
    CHECK(command.what == "sprite");
    CHECK(command.x == 10.0F);
    CHECK(command.y == 20.0F);
    CHECK(command.atlas == 2);
    CHECK(command.frame == 5U);
    CHECK(command.width == 32.0F);
    CHECK(command.height == 16.0F);
    CHECK(command.flip_x);
    CHECK(command.color == 0xFF8000C8U);
}

TEST_CASE("sprite2d: higher layers are drawn later, equal layers in creation order") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    const auto layered = [](std::int32_t layer, std::uint32_t frame) {
        sprite2d::Sprite sprite;
        sprite.layer = layer;
        sprite.frame = frame;
        return sprite;
    };
    spawn(app, 0.0F, 0.0F, layered(5, 0U));
    spawn(app, 0.0F, 0.0F, layered(-1, 1U));
    spawn(app, 0.0F, 0.0F, layered(5, 2U));
    spawn(app, 0.0F, 0.0F, layered(0, 3U));
    app.run_frames(1);
    REQUIRE(renderer->commands().size() == 4U);
    CHECK(renderer->commands()[0].frame == 1U); // layer -1
    CHECK(renderer->commands()[1].frame == 3U); // layer 0
    CHECK(renderer->commands()[2].frame == 0U); // layer 5, older
    CHECK(renderer->commands()[3].frame == 2U); // layer 5, newer
}

TEST_CASE("sprite2d: nothing is drawn without a position, and a missing renderer is fine") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    const ge::ecs::Entity no_position = app.world().spawn();
    CHECK(app.world().insert(no_position, sprite2d::Sprite{}));
    app.run_frames(1);
    CHECK(renderer->commands().empty());

    ge::app::App headless; // no renderer resource at all
    transform2d::install(headless);
    sprite2d::install(headless);
    spawn(headless, 0.0F, 0.0F, sprite2d::Sprite{});
    headless.run_frames(1);
}

TEST_CASE("sprite2d: colour channels above 255 are clamped") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    sprite2d::Sprite sprite;
    sprite.red = 1000U;
    sprite.green = 0U;
    sprite.blue = 0U;
    sprite.alpha = 99999U;
    spawn(app, 0.0F, 0.0F, sprite);
    app.run_frames(1);
    REQUIRE(renderer->commands().size() == 1U);
    CHECK(renderer->commands()[0].color == 0xFF0000FFU);
}

namespace {

/// The frame the sprite shows after `ticks` simulation ticks.
std::uint32_t frame_after(ge::app::App& app, ge::ecs::Entity entity, int ticks) {
    app.run_frames(static_cast<std::uint32_t>(ticks));
    const auto* sprite = app.world().get<sprite2d::Sprite>(entity);
    CHECK(sprite != nullptr);
    return sprite != nullptr ? sprite->frame : 0xFFFFFFFFU;
}

} // namespace

TEST_CASE("sprite2d: a looping animation steps through its cells and starts over") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    const ge::ecs::Entity entity = spawn(app, 0.0F, 0.0F, sprite2d::Sprite{});
    CHECK(app.world().insert(entity, sprite2d::SpriteAnimation{4U, 3U, 2U, true, 0U}));
    // Cells 4, 5, 6 for two ticks each: after n ticks, elapsed is n.
    CHECK(frame_after(app, entity, 1) == 4U);
    CHECK(frame_after(app, entity, 1) == 5U); // 2 ticks
    CHECK(frame_after(app, entity, 2) == 6U); // 4 ticks
    CHECK(frame_after(app, entity, 2) == 4U); // 6 ticks: back to the start
}

TEST_CASE("sprite2d: an animation that does not loop stays on its last cell") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    const ge::ecs::Entity entity = spawn(app, 0.0F, 0.0F, sprite2d::Sprite{});
    CHECK(app.world().insert(entity, sprite2d::SpriteAnimation{0U, 2U, 3U, false, 0U}));
    CHECK(frame_after(app, entity, 3) == 1U);
    CHECK(frame_after(app, entity, 100) == 1U);
}

TEST_CASE("sprite2d: odd animation settings do not break the animation") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    const ge::ecs::Entity entity = spawn(app, 0.0F, 0.0F, sprite2d::Sprite{});
    // Zero cells and zero ticks per cell behave like one of each.
    CHECK(app.world().insert(entity, sprite2d::SpriteAnimation{7U, 0U, 0U, true, 0U}));
    CHECK(frame_after(app, entity, 5) == 7U);
}

TEST_CASE("sprite2d: an animation is part of the simulation, so runs repeat exactly") {
    const auto run = [] {
        auto renderer = std::make_shared<ge::app::RecordingRenderer>();
        ge::app::App app = game(renderer);
        const ge::ecs::Entity entity = spawn(app, 0.0F, 0.0F, sprite2d::Sprite{});
        CHECK(app.world().insert(entity, sprite2d::SpriteAnimation{0U, 4U, 5U, true, 0U}));
        app.run_frames(37);
        return app.world().fingerprint();
    };
    CHECK(run() == run());
}
