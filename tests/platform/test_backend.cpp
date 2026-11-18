#include "ge/app/app.hpp"
#include "ge/app/host.hpp"
#include "ge/app/input.hpp"
#include "ge/app/render.hpp"
#include "ge/mod/action_control/action_control.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"
#include "ge/platform/backend.hpp"

#include "mini_test.hpp"

#include <memory>

using ge::platform::InputMap;
using ge::platform::ScriptedBackend;

namespace {

/// A ship steered by the move_x and move_y actions.
ge::app::App steered_ship() {
    ge::app::App app;
    ge::mod::transform2d::install(app);
    ge::mod::action_control::install(app);
    const ge::ecs::Entity ship = app.world().spawn();
    CHECK(app.world().insert(ship, ge::mod::transform2d::Position{}));
    CHECK(app.world().insert(ship, ge::mod::transform2d::Velocity{}));
    CHECK(app.world().insert(ship, ge::mod::action_control::Controlled{120.0F}));
    return app;
}

InputMap arrows() {
    InputMap map;
    map.bind("move_x", "key:right", 1.0F);
    map.bind("move_x", "key:left", -1.0F);
    return map;
}

} // namespace

TEST_CASE("backend: the scripted backend feeds one frame per tick") {
    ScriptedBackend backend(60, 3);
    std::vector<ge::platform::RawEvent> events;
    backend.at(1, "key:right", 1.0F);
    CHECK(backend.now().count() == 0);
    CHECK(backend.poll(events));
    CHECK(events.empty());
    CHECK(backend.now().count() == 16'666'666);
    CHECK(backend.poll(events));
    REQUIRE(events.size() == 1U);
    CHECK(events[0].source == "key:right");
    CHECK(backend.poll(events));
    CHECK(!backend.poll(events));
}

TEST_CASE("backend: a game plays the same with raw input as with scripted actions") {
    // Through the actions of the script, the way scenarios do it.
    ge::app::App scripted = steered_ship();
    auto* script = scripted.world().resource<ge::app::ScriptedInput>();
    script->add(0, "move_x", 1.0F);
    script->add(10, "move_x", 0.0F);
    scripted.run_frames(30);

    // Through a backend: the "player" presses and releases the right arrow.
    ge::app::App played = steered_ship();
    ScriptedBackend backend(60, 30);
    backend.at(0, "key:right", 1.0F);
    backend.at(10, "key:right", 0.0F);
    const auto result = ge::platform::run_loop(played, backend, arrows());

    CHECK(result.frames == 30U);
    CHECK(result.backend_stopped);
    CHECK(played.tick_count() == 30U);
    // Same movement. (The fingerprints differ: only the scripted run holds a script.)
    ge::mod::transform2d::Position from_script{};
    ge::mod::transform2d::Position from_keys{};
    scripted.world().each<const ge::mod::transform2d::Position>(
        [&](ge::ecs::Entity, const ge::mod::transform2d::Position& value) { from_script = value; });
    played.world().each<const ge::mod::transform2d::Position>(
        [&](ge::ecs::Entity, const ge::mod::transform2d::Position& value) { from_keys = value; });
    CHECK(from_keys.x > 0.0F);
    CHECK(from_keys == from_script);
}

TEST_CASE("backend: pressed and released are seen on the tick the key changes") {
    ge::app::App app;
    int pressed_ticks = 0;
    int released_ticks = 0;
    app.add_system(ge::app::Stage::FixedUpdate, [&](ge::ecs::World& world) {
        const auto* actions = world.resource<ge::app::Actions>();
        pressed_ticks += actions->pressed("move_x") ? 1 : 0;
        released_ticks += actions->released("move_x") ? 1 : 0;
    });
    ScriptedBackend backend(60, 10);
    backend.at(2, "key:right", 1.0F);
    backend.at(6, "key:right", 0.0F);
    ge::platform::run_loop(app, backend, arrows());
    CHECK(pressed_ticks == 1);
    CHECK(released_ticks == 1);
}

TEST_CASE("backend: the game draws and plays through the backend's devices") {
    ge::app::App app;
    app.add_system(ge::app::Stage::Update, [](ge::ecs::World& world) {
        world.resource<ge::app::RenderBackend>()->renderer().draw({"ship", 1.0F, 2.0F});
        world.resource<ge::app::AudioBackend>()->device().play("beep");
    });
    ScriptedBackend backend(60, 4);
    ge::platform::run_loop(app, backend, InputMap{});
    CHECK(backend.drawn().commands().size() == 4U);
    CHECK(backend.drawn().frames() == 4U);
    REQUIRE(backend.heard().played().size() == 4U);
    CHECK(backend.heard().played()[0] == "beep");
}

TEST_CASE("backend: max_frames stops a loop the backend would not stop") {
    ge::app::App app;
    ScriptedBackend backend(60, 100);
    ge::platform::LoopOptions options;
    options.max_frames = 5;
    const auto result = ge::platform::run_loop(app, backend, InputMap{}, options);
    CHECK(result.frames == 5U);
    CHECK(!result.backend_stopped);
    CHECK(app.tick_count() == 5U);
}

TEST_CASE("backend: a quit request ends the loop after the frame that raised it") {
    ge::app::App app;
    app.add_system(ge::app::Stage::Update, [](ge::ecs::World& world) {
        if (world.resource<ge::app::Time>()->tick == 2U) {
            world.resource<ge::app::QuitRequest>()->requested = true;
        }
    });
    ScriptedBackend backend(60, 100);
    const auto result = ge::platform::run_loop(app, backend, InputMap{});
    CHECK(result.quit_requested);
    CHECK(!result.back_requested);
    CHECK(!result.backend_stopped);
    CHECK(result.frames < 10U);
}

TEST_CASE("backend: a back source ends the loop before the game sees it") {
    ge::app::App app;
    ScriptedBackend backend(60, 100);
    backend.at(3, "key:escape", 1.0F);
    ge::platform::LoopOptions options;
    options.back_sources = {"key:escape"};
    const auto result = ge::platform::run_loop(app, backend, InputMap{}, options);
    CHECK(result.back_requested);
    CHECK(!result.quit_requested);
    CHECK(result.frames == 3U);
}

TEST_CASE("backend: without back sources the escape key is just a key") {
    ge::app::App app;
    ScriptedBackend backend(60, 10);
    backend.at(3, "key:escape", 1.0F);
    const auto result = ge::platform::run_loop(app, backend, InputMap{});
    CHECK(!result.back_requested);
    CHECK(result.backend_stopped);
}

TEST_CASE("backend: a scripted backend remembers the size it was resized to") {
    ScriptedBackend backend(60, 1);
    CHECK(backend.width() == 0);
    CHECK(backend.resize(480, 800));
    CHECK(backend.width() == 480);
    CHECK(backend.height() == 800);
}

TEST_CASE("audio: the recorder remembers requests, the null device ignores them") {
    ge::app::RecordingAudio recorder;
    recorder.play("a");
    recorder.play("b");
    CHECK(recorder.played().size() == 2U);
    recorder.clear();
    CHECK(recorder.played().empty());
    ge::app::NullAudio null_device;
    null_device.play("a");
    ge::app::App app;
    REQUIRE(app.world().resource<ge::app::AudioBackend>() != nullptr);
    app.world().resource<ge::app::AudioBackend>()->device().play("anything");
}
