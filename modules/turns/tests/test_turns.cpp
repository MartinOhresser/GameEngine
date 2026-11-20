#include "ge/app/app.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/mod/turns/turns.gen.hpp"

#include "mini_test.hpp"

namespace {

namespace turns = ge::mod::turns;

struct Scene {
    ge::app::App app;
    ge::ecs::Entity entity;

    explicit Scene(std::uint32_t players) {
        turns::install(app);
        CHECK(app.module_problems().empty());
        CHECK(app.schedule().problems().empty());
        app.world().insert_resource(ge::app::ActionNames{{"end", "changed"}});
        entity = app.world().spawn();
        turns::Turn turn;
        turn.players = players;
        turn.end_action = 0;
        turn.changed_action = 1;
        CHECK(app.world().insert(entity, turn));
    }

    void end_turn(std::uint64_t tick) {
        auto* input = app.world().resource<ge::app::ScriptedInput>();
        input->add(tick, "end", 1.0F);
        input->add(tick + 1, "end", 0.0F);
    }

    [[nodiscard]] const turns::Turn& turn() const { return *app.world().get<turns::Turn>(entity); }
};

} // namespace

TEST_CASE("turns: the turn goes to the next player and comes back to the first") {
    Scene scene(3);
    CHECK(scene.turn().current == 0);
    scene.end_turn(1);
    scene.end_turn(4);
    scene.end_turn(7);
    scene.app.run_frames(3);
    CHECK(scene.turn().current == 1);
    CHECK(scene.turn().number == 1);
    scene.app.run_frames(3);
    CHECK(scene.turn().current == 2);
    scene.app.run_frames(3);
    CHECK(scene.turn().current == 0);
    CHECK(scene.turn().number == 3);
}

TEST_CASE("turns: nothing changes without the action, and the change is announced") {
    Scene scene(2);
    scene.app.run_frames(5);
    CHECK(scene.turn().number == 0);
    scene.end_turn(6);
    bool announced = false;
    for (int i = 0; i < 4; ++i) {
        scene.app.run_frames(1);
        announced = announced || scene.app.world().resource<ge::app::Actions>()->pressed("changed");
    }
    CHECK(announced);
    CHECK(scene.turn().current == 1);
}
