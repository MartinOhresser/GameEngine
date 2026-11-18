#include "ge/app/app.hpp"
#include "ge/app/input.hpp"

#include "../support/mini_test.hpp"

using ge::app::Actions;
using ge::app::App;
using ge::app::ScriptedInput;
using ge::app::Stage;

TEST_CASE("actions: unset actions are zero, values are kept") {
    Actions actions;
    CHECK(actions.value("jump") == 0.0F);
    CHECK(!actions.down("jump"));
    actions.set("move_x", -0.5F);
    CHECK(actions.value("move_x") == -0.5F);
    CHECK(actions.down("move_x"));
    actions.set("move_x", 1.0F);
    CHECK(actions.value("move_x") == 1.0F);
    CHECK(actions.values().size() == 1U);
}

TEST_CASE("actions: pressed and released last one tick") {
    Actions actions;
    actions.begin_tick();
    actions.set("jump", 1.0F);
    CHECK(actions.pressed("jump"));
    CHECK(!actions.released("jump"));
    actions.begin_tick();
    CHECK(!actions.pressed("jump")); // still held, no longer new
    CHECK(actions.down("jump"));
    actions.begin_tick();
    actions.set("jump", 0.0F);
    CHECK(actions.released("jump"));
    CHECK(!actions.pressed("jump"));
    actions.begin_tick();
    CHECK(!actions.released("jump"));
}

TEST_CASE("actions: the hash depends on the values") {
    Actions a;
    const Actions b;
    ge::Hasher first;
    ge::Hasher second;
    a.hash(first);
    b.hash(second);
    CHECK(first.value() == second.value());
    a.set("jump", 1.0F);
    ge::Hasher third;
    a.hash(third);
    CHECK(third.value() != first.value());
}

TEST_CASE("scripted input: events apply at their tick, in order") {
    ScriptedInput script;
    script.add(5, "jump", 1.0F);
    script.add(2, "move_x", 1.0F);
    script.add(5, "jump", 0.0F); // same tick: the later one wins
    script.add(9, "move_x", 0.0F);
    CHECK(script.size() == 4U);
    Actions actions;
    script.apply(1, actions);
    CHECK(actions.values().empty());
    script.apply(2, actions);
    CHECK(actions.value("move_x") == 1.0F);
    script.apply(5, actions);
    CHECK(actions.value("jump") == 0.0F);
    script.apply(8, actions);
    CHECK(actions.value("move_x") == 1.0F);
    script.apply(9, actions);
    CHECK(actions.value("move_x") == 0.0F);
}

TEST_CASE("app: the script drives the actions tick by tick") {
    App app;
    app.world().resource<ScriptedInput>()->add(0, "move_x", 1.0F);
    app.world().resource<ScriptedInput>()->add(3, "move_x", 0.0F);
    int seen = 0;
    int pressed_ticks = 0;
    app.add_system(
        Stage::FixedUpdate,
        [&](ge::ecs::World& world) {
            const auto* actions = world.resource<Actions>();
            seen += actions->down("move_x") ? 1 : 0;
            pressed_ticks += actions->pressed("move_x") ? 1 : 0;
        },
        "test.read");
    app.run_frames(6);
    CHECK(seen == 3);          // ticks 0, 1 and 2
    CHECK(pressed_ticks == 1); // only tick 0
}

TEST_CASE("app: the same script gives the same world") {
    const auto run = [](float strength) {
        App app;
        app.world().resource<ScriptedInput>()->add(2, "move_x", strength);
        app.run_frames(5);
        return app.world().fingerprint();
    };
    CHECK(run(1.0F) == run(1.0F));
    CHECK(run(1.0F) != run(0.5F));
}
