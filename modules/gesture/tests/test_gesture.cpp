#include "ge/app/app.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/mod/gesture/gesture.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

#include <string>

namespace {

namespace gesture = ge::mod::gesture;
namespace transform2d = ge::mod::transform2d;

struct Scene {
    ge::app::App app;
    ge::ecs::Entity watcher;

    Scene() {
        transform2d::install(app);
        gesture::install(app);
        CHECK(app.module_problems().empty());
        CHECK(app.schedule().problems().empty());
        app.world().insert_resource(ge::app::ActionNames{
            {"pointer_x", "pointer_y", "press", "go_left", "go_right", "go_up", "go_down"}});
        watcher = app.world().spawn();
        gesture::Swipe swipe;
        swipe.pointer_x = 0;
        swipe.pointer_y = 1;
        swipe.press = 2;
        swipe.left = 3;
        swipe.right = 4;
        swipe.up = 5;
        swipe.down = 6;
        swipe.min_distance = 40.0F;
        swipe.max_ticks = 10;
        CHECK(app.world().insert(watcher, swipe));
    }

    /// A finger goes down at (x0, y0) on tick 1 and up at (x1, y1) `lasting` ticks later; the
    /// actions that were raised are collected until the game has run a few ticks more.
    std::string swipe(float x0, float y0, float x1, float y1, std::uint64_t lasting) {
        auto* input = app.world().resource<ge::app::ScriptedInput>();
        input->add(1, "pointer_x", x0);
        input->add(1, "pointer_y", y0);
        input->add(1, "press", 1.0F);
        input->add(1 + lasting, "pointer_x", x1);
        input->add(1 + lasting, "pointer_y", y1);
        input->add(1 + lasting, "press", 0.0F);
        std::string seen;
        for (std::uint64_t tick = 0; tick < lasting + 6; ++tick) {
            app.run_frames(1);
            for (const char* name : {"go_left", "go_right", "go_up", "go_down"}) {
                if (app.world().resource<ge::app::Actions>()->pressed(name)) {
                    seen += std::string(name) + " ";
                }
            }
        }
        return seen;
    }
};

} // namespace

TEST_CASE("gesture: a swipe raises the action of the direction it went in") {
    Scene right;
    CHECK(right.swipe(100.0F, 100.0F, 180.0F, 110.0F, 5) == "go_right ");
    Scene left;
    CHECK(left.swipe(200.0F, 100.0F, 120.0F, 90.0F, 5) == "go_left ");
    Scene up;
    CHECK(up.swipe(100.0F, 300.0F, 110.0F, 200.0F, 5) == "go_up "); // y grows downwards
    Scene down;
    CHECK(down.swipe(100.0F, 100.0F, 90.0F, 200.0F, 5) == "go_down ");
}

TEST_CASE("gesture: the axis with the bigger movement counts") {
    Scene scene;
    CHECK(scene.swipe(100.0F, 100.0F, 170.0F, 150.0F, 5) == "go_right ");
}

TEST_CASE("gesture: a movement that is too short or too slow is not a swipe") {
    Scene tap;
    CHECK(tap.swipe(100.0F, 100.0F, 110.0F, 105.0F, 3).empty());
    Scene slow;
    CHECK(slow.swipe(100.0F, 100.0F, 220.0F, 100.0F, 25).empty()); // max_ticks is 10
    Scene exact;
    CHECK(exact.swipe(100.0F, 100.0F, 140.0F, 100.0F, 10) == "go_right "); // 40 pixels, 10 ticks
}

TEST_CASE("gesture: a direction with no action raises nothing") {
    Scene scene;
    scene.app.world().get<gesture::Swipe>(scene.watcher)->right = 4294967295U;
    CHECK(scene.swipe(100.0F, 100.0F, 200.0F, 100.0F, 5).empty());
}

TEST_CASE("gesture: a Disabled Swipe does nothing, and forgets a swipe begun before") {
    Scene scene;
    CHECK(scene.app.world().insert(scene.watcher, transform2d::Disabled{}));
    CHECK(scene.swipe(100.0F, 100.0F, 200.0F, 100.0F, 5).empty());
}

TEST_CASE("gesture: a swipe after another one works too") {
    Scene scene;
    CHECK(scene.swipe(100.0F, 100.0F, 200.0F, 100.0F, 5) == "go_right ");
    auto* input = scene.app.world().resource<ge::app::ScriptedInput>();
    const std::uint64_t base = scene.app.tick_count();
    input->add(base + 1, "pointer_x", 200.0F);
    input->add(base + 1, "press", 1.0F);
    input->add(base + 4, "pointer_x", 100.0F);
    input->add(base + 4, "press", 0.0F);
    std::string seen;
    for (int i = 0; i < 8; ++i) {
        scene.app.run_frames(1);
        seen += scene.app.world().resource<ge::app::Actions>()->pressed("go_left") ? "L" : "";
    }
    CHECK(seen == "L");
}
