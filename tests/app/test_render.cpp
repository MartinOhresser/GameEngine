#include "ge/app/app.hpp"
#include "ge/app/render.hpp"

#include "../support/mini_test.hpp"

#include <memory>

using ge::app::App;
using ge::app::DrawCommand;
using ge::app::NullRenderer;
using ge::app::RecordingRenderer;
using ge::app::RenderBackend;
using ge::app::Stage;

namespace {

struct Ship {
    float x = 0.0F;
};

/// A game: a ship moves right each tick. A presentation system, in Update,
/// asks the renderer to draw it. The simulation never touches the renderer.
App ship_game(std::shared_ptr<ge::app::Renderer> backend) {
    App app;
    app.world().insert_resource(RenderBackend{std::move(backend)});
    const ge::ecs::Entity ship = app.world().spawn();
    CHECK(app.world().insert(ship, Ship{}));
    app.add_system(Stage::FixedUpdate, [](ge::ecs::World& world) {
        world.each<Ship>([](ge::ecs::Entity, Ship& value) { value.x += 1.0F; });
    });
    app.add_system(Stage::Update, [](ge::ecs::World& world) {
        auto& renderer = world.resource<RenderBackend>()->renderer();
        world.each<const Ship>([&renderer](ge::ecs::Entity, const Ship& value) {
            renderer.draw(DrawCommand{"ship", value.x, 0.0F});
        });
        renderer.end_frame();
    });
    return app;
}

} // namespace

TEST_CASE("render: an app draws to a null renderer by default") {
    App app;
    REQUIRE(app.world().resource<RenderBackend>() != nullptr);
    // Nothing to see, nothing to crash: drawing to the default renderer is allowed.
    auto& renderer = app.world().resource<RenderBackend>()->renderer();
    renderer.draw(DrawCommand{"anything", 1.0F, 2.0F});
    renderer.end_frame();
    app.run_frames(2);
    CHECK(app.tick_count() == 2U);
}

TEST_CASE("render: the recorder remembers what the game asked for, frame by frame") {
    auto recorder = std::make_shared<RecordingRenderer>();
    App app = ship_game(recorder);
    app.run_frames(3);
    REQUIRE(recorder->commands().size() == 3U);
    const DrawCommand first{"ship", 1.0F, 0.0F};
    CHECK(recorder->commands()[0] == first);
    CHECK(recorder->commands()[2].x == 3.0F);
    CHECK(recorder->frames() == 3U);
    recorder->clear();
    CHECK(recorder->commands().empty());
}

TEST_CASE("render: the simulation ends in the same state whatever renderer is attached") {
    const auto run = [](std::shared_ptr<ge::app::Renderer> renderer) {
        App app = ship_game(std::move(renderer));
        app.run_frames(30);
        return app.world().fingerprint();
    };
    const std::uint64_t with_nothing = run(std::make_shared<NullRenderer>());
    const std::uint64_t with_recorder = run(std::make_shared<RecordingRenderer>());
    CHECK(with_nothing == with_recorder);
}
