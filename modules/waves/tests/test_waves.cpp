#include "ge/app/app.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/app/spawn.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"
#include "ge/mod/waves/waves.gen.hpp"

#include "mini_test.hpp"

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace {

namespace transform2d = ge::mod::transform2d;
namespace waves = ge::mod::waves;

using ge::ecs::Entity;

constexpr std::uint32_t go_action = 0;
constexpr std::uint32_t started_action = 1;
constexpr std::uint32_t cleared_action = 2;
constexpr std::uint32_t all_cleared_action = 3;

// Actions: 0 go, 1 started, 2 cleared, 3 all_cleared.
struct Scene {
    ge::app::App app;
    /// What was created, with the tick it was asked for.
    std::shared_ptr<std::vector<Entity>> made = std::make_shared<std::vector<Entity>>();
    Entity controller;

    Scene() {
        transform2d::install(app);
        waves::install(app);
        CHECK(app.module_problems().empty());
        CHECK(app.schedule().problems().empty());
        app.world().insert_resource(
            ge::app::ActionNames{{"go", "started", "cleared", "all_cleared"}});
        app.set_prefab_spawner(
            [made = made](ge::app::App& game, const ge::app::SpawnRequest& request) {
                const Entity entity = game.world().spawn();
                CHECK(game.world().insert(entity, transform2d::Position{request.x, request.y}));
                CHECK(game.world().insert(entity, transform2d::Owner{request.owner}));
                made->push_back(entity);
            });
        controller = app.world().spawn();
        waves::WaveController setup;
        setup.spawn_x = 7.0F;
        setup.spawn_y = 9.0F;
        setup.start_action = go_action;
        setup.started_action = started_action;
        setup.cleared_action = cleared_action;
        setup.all_cleared_action = all_cleared_action;
        CHECK(app.world().insert(controller, setup));
    }

    Entity group(std::uint32_t wave, std::uint32_t count, std::uint32_t interval,
                 std::uint32_t delay = 0) {
        const Entity entity = app.world().spawn();
        waves::WaveGroup group_setup;
        group_setup.wave = wave;
        group_setup.prefab = 5;
        group_setup.count = count;
        group_setup.interval_ticks = interval;
        group_setup.delay_ticks = delay;
        CHECK(app.world().insert(entity, group_setup));
        return entity;
    }

    waves::WaveController& state() { return *app.world().get<waves::WaveController>(controller); }

    /// Presses the start action for one tick.
    void go() { app.world().resource<ge::app::Actions>()->raise("go"); }

    [[nodiscard]] bool pressed(std::string_view name) const {
        return app.world().resource<ge::app::Actions>()->pressed(name);
    }

    /// Runs `frames` ticks and counts the ticks in which the action was pressed.
    int count(std::string_view name, int frames) {
        int seen = 0;
        for (int i = 0; i < frames; ++i) {
            app.run_frames(1);
            seen += pressed(name) ? 1 : 0;
        }
        return seen;
    }

    /// Removes everything that was created (the monsters are all dead).
    void kill_all() {
        for (const Entity entity : *made) {
            if (app.world().is_alive(entity)) {
                CHECK(app.world().despawn(entity));
            }
        }
    }
};

} // namespace

TEST_CASE("waves: nothing happens until the start action") {
    Scene scene;
    scene.group(1, 3, 10);
    scene.app.run_frames(30);
    CHECK(scene.made->empty());
    CHECK(!scene.state().running);
    CHECK(scene.state().last == 1U);
}

TEST_CASE(
    "waves: a group creates its things from the delay, one per interval, at the spawn point") {
    Scene scene;
    scene.group(1, 3, 10, 5);
    scene.go();
    scene.app.run_frames(2); // raised in tick 1, pressed in tick 2: the wave starts
    CHECK(scene.state().current == 1U);
    CHECK(scene.state().running);
    scene.app.run_frames(2);
    CHECK(scene.made->empty()); // the delay is not over
    scene.app.run_frames(30);
    REQUIRE(scene.made->size() == 3U);
    const auto* at = scene.app.world().get<transform2d::Position>(scene.made->front());
    REQUIRE(at != nullptr);
    CHECK(at->x == 7.0F);
    CHECK(at->y == 9.0F);
}

TEST_CASE("waves: the wave is cleared when what it made is gone, once") {
    Scene scene;
    scene.group(1, 2, 5);
    scene.group(1, 1, 0, 8);
    scene.go();
    scene.app.run_frames(40);
    CHECK(scene.made->size() == 3U);
    CHECK(scene.state().running); // they are still alive
    scene.kill_all();
    CHECK(scene.count("cleared", 8) == 1);
    CHECK(!scene.state().running);
    CHECK(scene.state().finished); // only one wave: it was the last one
}

TEST_CASE("waves: a wave started by the action is announced, and the next one follows on demand") {
    Scene scene;
    scene.group(1, 1, 0);
    scene.group(2, 2, 0);
    scene.go();
    CHECK(scene.count("started", 3) == 1);
    scene.app.run_frames(10);
    scene.kill_all();
    scene.app.run_frames(10);
    CHECK(!scene.state().running);
    CHECK(!scene.state().finished);
    CHECK(scene.state().current == 1U);
    CHECK(scene.count("all_cleared", 5) == 0);
    scene.go();
    scene.app.run_frames(10);
    CHECK(scene.state().current == 2U);
    CHECK(scene.made->size() == 3U); // one from the first wave, two from the second
}

TEST_CASE("waves: the last wave raises all_cleared, once, and nothing starts after it") {
    Scene scene;
    scene.group(1, 1, 0);
    scene.go();
    scene.app.run_frames(10);
    scene.kill_all();
    CHECK(scene.count("all_cleared", 10) == 1);
    CHECK(scene.state().finished);
    scene.go();
    scene.app.run_frames(10);
    CHECK(scene.state().current == 1U);
    CHECK(scene.made->size() == 1U);
}

TEST_CASE("waves: auto_next starts the next wave after the rest") {
    Scene scene;
    scene.state().auto_next = true;
    scene.state().rest_ticks = 20;
    scene.group(1, 1, 0);
    scene.group(2, 1, 0);
    scene.go();
    scene.app.run_frames(10);
    scene.kill_all();
    scene.app.run_frames(10); // cleared, resting
    CHECK(scene.state().current == 1U);
    scene.app.run_frames(30);
    CHECK(scene.state().current == 2U);
    CHECK(scene.state().running);
}

TEST_CASE("waves: a wave with groups of other waves ignores them") {
    Scene scene;
    scene.group(1, 1, 0);
    scene.group(2, 5, 0);
    scene.go();
    scene.app.run_frames(10);
    CHECK(scene.made->size() == 1U);
}

TEST_CASE("waves: a Disabled controller waits") {
    Scene scene;
    scene.group(1, 1, 0);
    CHECK(scene.app.world().insert(scene.controller, transform2d::Disabled{}));
    scene.go();
    scene.app.run_frames(10);
    CHECK(scene.made->empty());
}
