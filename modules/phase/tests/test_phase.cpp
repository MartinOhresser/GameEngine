#include "ge/app/app.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/mod/phase/phase.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

namespace {

namespace phase = ge::mod::phase;
namespace transform2d = ge::mod::transform2d;

constexpr std::uint32_t play = 0;
constexpr std::uint32_t won = 1;
constexpr std::uint32_t paused = 2;

struct Scene {
    ge::app::App app;
    ge::ecs::Entity holder;

    Scene() {
        transform2d::install(app);
        phase::install(app);
        CHECK(app.module_problems().empty());
        CHECK(app.schedule().problems().empty());
        app.world().insert_resource(ge::app::ActionNames{{"win", "pause", "resume", "restart"}});
        holder = app.world().spawn();
        CHECK(app.world().insert(holder, phase::Phase{play}));
    }

    [[nodiscard]] std::uint32_t current() const {
        return app.world().get<phase::Phase>(holder)->current;
    }

    ge::ecs::Entity with_transition(std::uint32_t action, std::uint32_t from, bool any,
                                    std::uint32_t to) {
        const auto entity = app.world().spawn();
        CHECK(app.world().insert(entity, phase::Transition{action, from, any, to}));
        return entity;
    }

    void press(const char* action) {
        app.world().resource<ge::app::Actions>()->raise(action);
        app.run_frames(3);
    }
};

} // namespace

TEST_CASE("phase: an action moves the game to another phase") {
    Scene scene;
    scene.with_transition(0, 0, true, won);
    CHECK(scene.current() == play);
    scene.app.run_frames(3);
    CHECK(scene.current() == play);
    scene.press("win");
    CHECK(scene.current() == won);
}

TEST_CASE("phase: a transition only applies from the phases it lists") {
    Scene scene;
    scene.with_transition(2, 1U << paused, false, play); // resume: only out of a pause
    scene.press("resume");
    CHECK(scene.current() == play); // not paused: nothing happens
    scene.with_transition(1, 1U << play, false, paused);
    scene.press("pause");
    CHECK(scene.current() == paused);
    scene.press("resume");
    CHECK(scene.current() == play);
}

TEST_CASE("phase: an entity shown in some phases is hidden in the others") {
    Scene scene;
    scene.with_transition(0, 0, true, won);
    scene.with_transition(3, 0, true, play);
    const auto banner = scene.app.world().spawn();
    CHECK(scene.app.world().insert(banner, phase::ShowIn{1U << won}));
    const auto board = scene.app.world().spawn();
    CHECK(scene.app.world().insert(board, phase::ShowIn{(1U << play) | (1U << won)}));
    scene.app.run_frames(2);
    CHECK(scene.app.world().has<transform2d::Hidden>(banner));
    CHECK(!scene.app.world().has<transform2d::Hidden>(board));
    scene.press("win");
    CHECK(!scene.app.world().has<transform2d::Hidden>(banner));
    CHECK(!scene.app.world().has<transform2d::Hidden>(board));
    scene.press("restart");
    CHECK(scene.app.world().has<transform2d::Hidden>(banner));
}

TEST_CASE("phase: an entity active in some phases is switched off in the others") {
    Scene scene;
    scene.with_transition(0, 0, true, won);
    const auto board = scene.app.world().spawn();
    CHECK(scene.app.world().insert(board, phase::ActiveIn{1U << play}));
    scene.app.run_frames(2);
    CHECK(!scene.app.world().has<transform2d::Disabled>(board));
    scene.press("win");
    CHECK(scene.app.world().has<transform2d::Disabled>(board));
    CHECK(!scene.app.world().has<transform2d::Hidden>(board)); // still shown
}

TEST_CASE("phase: a game with no Phase entity leaves everything alone") {
    ge::app::App app;
    transform2d::install(app);
    phase::install(app);
    app.world().insert_resource(ge::app::ActionNames{{"go"}});
    const auto entity = app.world().spawn();
    CHECK(app.world().insert(entity, phase::ShowIn{1U}));
    app.run_frames(3);
    CHECK(!app.world().has<transform2d::Hidden>(entity));
}
