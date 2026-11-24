#include "ge/app/app.hpp"
#include "ge/app/render.hpp"
#include "ge/mod/light2d/light2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>

namespace {

namespace light2d = ge::mod::light2d;
namespace transform2d = ge::mod::transform2d;

ge::app::App game(const std::shared_ptr<ge::app::RecordingRenderer>& renderer) {
    ge::app::App app;
    transform2d::install(app);
    light2d::install(app);
    CHECK(app.module_problems().empty());
    CHECK(app.schedule().problems().empty());
    app.world().insert_resource(ge::app::RenderBackend{renderer});
    return app;
}

ge::ecs::Entity spawn_light(ge::app::App& app, float x, float y, const light2d::Light& light) {
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{x, y}));
    CHECK(app.world().insert(entity, light));
    return entity;
}

/// The alpha (the strength) of a 0xRRGGBBAA colour.
std::uint32_t alpha_of(std::uint32_t color) {
    return color & 0xFFU;
}

} // namespace

TEST_CASE("light2d: without an ambient there is no lighting") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    app.run_frames(1);
    CHECK(!renderer->lighting().has_value());
}

TEST_CASE("light2d: the oldest ambient sets the colour of the dark") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    const auto first = app.world().spawn();
    CHECK(app.world().insert(first, light2d::Ambient{20U, 30U, 300U}));
    const auto second = app.world().spawn();
    CHECK(app.world().insert(second, light2d::Ambient{255U, 0U, 0U}));
    app.run_frames(1);
    const ge::app::Lighting lighting = renderer->lighting().value_or(ge::app::Lighting{});
    CHECK(lighting.on);
    CHECK(lighting.ambient == 0x141EFFFFU); // 300 is kept to 255
}

TEST_CASE("light2d: a light is a glow of its colour in the light map, at its offset") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    light2d::Light lamp;
    lamp.radius = 80.0F;
    lamp.red = 255U;
    lamp.green = 200U;
    lamp.blue = 100U;
    lamp.intensity = 0.5F;
    lamp.offset_y = -30.0F;
    spawn_light(app, 100.0F, 200.0F, lamp);
    app.run_frames(1);
    REQUIRE(renderer->commands().size() == 1U);
    const ge::app::DrawCommand& command = renderer->commands()[0];
    CHECK(command.what == "glow");
    CHECK(command.light);
    CHECK(command.blend == ge::app::Blend::Add);
    CHECK(command.x == 100.0F);
    CHECK(command.y == 170.0F);
    CHECK(command.size == 80.0F);
    CHECK(command.color == 0xFFC86480U); // half strength: 128
}

TEST_CASE("light2d: a flickering light wavers within its range, each in its own way") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    light2d::Light fire;
    fire.flicker = 0.4F;
    spawn_light(app, 0.0F, 0.0F, fire);
    spawn_light(app, 50.0F, 0.0F, fire);
    std::uint32_t lowest = 255U;
    std::uint32_t highest = 0U;
    bool apart = false;
    for (int frame = 0; frame < 120; ++frame) {
        renderer->clear();
        app.run_frames(1);
        REQUIRE(renderer->commands().size() == 2U);
        const std::uint32_t one = alpha_of(renderer->commands()[0].color);
        const std::uint32_t other = alpha_of(renderer->commands()[1].color);
        lowest = std::min(lowest, one);
        highest = std::max(highest, one);
        apart = apart || one != other;
    }
    CHECK(lowest >= 152U); // 255 * (1 - 0.4), rounded down
    CHECK(lowest < 200U);
    CHECK(highest > 240U);
    CHECK(apart);
}
