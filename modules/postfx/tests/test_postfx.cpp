#include "ge/app/app.hpp"
#include "ge/app/render.hpp"
#include "ge/mod/postfx/postfx.gen.hpp"

#include "mini_test.hpp"

#include <memory>

namespace {

namespace postfx = ge::mod::postfx;

ge::app::App game(const std::shared_ptr<ge::app::RecordingRenderer>& renderer) {
    ge::app::App app;
    postfx::install(app);
    CHECK(app.module_problems().empty());
    CHECK(app.schedule().problems().empty());
    app.world().insert_resource(ge::app::RenderBackend{renderer});
    return app;
}

} // namespace

TEST_CASE("postfx: without Effects the renderer is told nothing") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    app.run_frames(1);
    CHECK(!renderer->effects().has_value());
}

namespace {

/// What the renderer is told when the oldest of two Effects is `look`.
ge::app::ScreenEffects told(const postfx::Effects& look) {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    const auto first = app.world().spawn();
    CHECK(app.world().insert(first, look));
    const auto second = app.world().spawn();
    CHECK(app.world().insert(second, postfx::Effects{}));
    app.run_frames(1);
    return renderer->effects().value_or(ge::app::ScreenEffects{});
}

} // namespace

TEST_CASE("postfx: the oldest Effects reach the renderer, kept within their ranges") {
    postfx::Effects look;
    look.bloom = 0.4F;
    look.vignette = 2.0F; // more than 1: 1
    look.vignette_size = 0.5F;
    look.grain = 0.1F;
    look.letterbox = 0.2F;
    look.rays = 3.0F; // more than 1: 1
    look.rays_x = 0.25F;
    const ge::app::ScreenEffects effects = told(look);
    CHECK(effects.rays == 1.0F);
    CHECK(effects.rays_x == 0.25F);
    CHECK(effects.rays_y == 0.35F);
    CHECK(effects.bloom == 0.4F);
    CHECK(effects.vignette == 1.0F);
    CHECK(effects.vignette_size == 0.5F);
    CHECK(effects.grain == 0.1F);
    CHECK(effects.letterbox == 0.2F);
}

TEST_CASE("postfx: the blur of a paused game replaces the usual one") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    postfx::Effects look;
    look.blur = 0.1F;
    look.paused_blur = 2.0F; // more than 1: 1
    CHECK(app.world().insert(app.world().spawn(), look));
    app.run_frames(1);
    CHECK(renderer->effects().value_or(ge::app::ScreenEffects{}).blur == 0.1F);
    app.world().resource<ge::app::Time>()->paused = true;
    app.run_frames(1);
    CHECK(renderer->effects().value_or(ge::app::ScreenEffects{}).blur == 1.0F);
}

TEST_CASE("postfx: the tint and the lift are colours, each channel kept to 255") {
    postfx::Effects look;
    look.tint_red = 200U;
    look.tint_green = 220U;
    look.tint_blue = 300U; // more than 255: 255
    look.lift_blue = 16U;
    const ge::app::ScreenEffects effects = told(look);
    CHECK(effects.tint == 0xC8DCFFFFU);
    CHECK(effects.lift == 0x000010FFU);
    CHECK(effects.any());
}

TEST_CASE("postfx: default Effects ask for nothing") {
    CHECK(!ge::app::ScreenEffects{}.any());
    ge::app::ScreenEffects lifted;
    lifted.lift = 0x000001FFU;
    CHECK(lifted.any());
}
