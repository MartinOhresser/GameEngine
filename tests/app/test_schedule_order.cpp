#include "ge/app/app.hpp"

#include "../support/mini_test.hpp"

#include <string>
#include <vector>

using ge::app::App;
using ge::app::Order;
using ge::app::Schedule;
using ge::app::Stage;
using ge::ecs::World;

namespace {

void nothing(World& /*world*/) {}

std::vector<std::string> names(const Schedule& schedule, Stage stage = Stage::Update) {
    return schedule.system_names(stage);
}

} // namespace

TEST_CASE("order: systems without constraints keep registration order") {
    Schedule schedule;
    schedule.add_system(Stage::Update, nothing, "a");
    schedule.add_system(Stage::Update, nothing, "b");
    schedule.add_system(Stage::Update, nothing, "c");
    CHECK(names(schedule) == (std::vector<std::string>{"a", "b", "c"}));
    CHECK(schedule.problems().empty());
}

TEST_CASE("order: after delays a system until its predecessor has run") {
    Schedule schedule;
    schedule.add_system(Stage::Update, nothing, "movement", Order::after({"input"}));
    schedule.add_system(Stage::Update, nothing, "input");
    CHECK(names(schedule) == (std::vector<std::string>{"input", "movement"}));
}

TEST_CASE("order: before moves a system ahead of its successor") {
    Schedule schedule;
    schedule.add_system(Stage::Update, nothing, "render");
    schedule.add_system(Stage::Update, nothing, "camera", Order::before({"render"}));
    CHECK(names(schedule) == (std::vector<std::string>{"camera", "render"}));
}

TEST_CASE("order: unconstrained systems stay in place around a constrained one") {
    Schedule schedule;
    schedule.add_system(Stage::Update, nothing, "a", Order::after({"c"}));
    schedule.add_system(Stage::Update, nothing, "b");
    schedule.add_system(Stage::Update, nothing, "c");
    CHECK(names(schedule) == (std::vector<std::string>{"b", "c", "a"}));
}

TEST_CASE("order: chains are followed through") {
    Schedule schedule;
    schedule.add_system(Stage::Update, nothing, "third", Order::after({"second"}));
    schedule.add_system(Stage::Update, nothing, "second", Order::after({"first"}));
    schedule.add_system(Stage::Update, nothing, "first");
    CHECK(names(schedule) == (std::vector<std::string>{"first", "second", "third"}));
}

TEST_CASE("order: constraints apply when the schedule runs") {
    App app;
    struct Log {
        std::vector<std::string> lines;
    };
    app.insert_resource(Log{});
    const auto record = [](const char* text) {
        return [text](World& world) { world.resource<Log>()->lines.emplace_back(text); };
    };
    app.add_system(Stage::Update, record("second"), "second", Order::after({"first"}));
    app.add_system(Stage::Update, record("first"), "first");
    app.update();
    const auto& lines = app.world().resource<Log>()->lines;
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "first");
    CHECK(lines[1] == "second");
}

TEST_CASE("order: adding a system after the schedule ran is taken into account") {
    App app;
    struct Log {
        std::vector<std::string> lines;
    };
    app.insert_resource(Log{});
    const auto record = [](const char* text) {
        return [text](World& world) { world.resource<Log>()->lines.emplace_back(text); };
    };
    app.add_system(Stage::Update, record("late"), "late");
    app.update();
    app.add_system(Stage::Update, record("early"), "early", Order::before({"late"}));
    app.update();
    const auto& lines = app.world().resource<Log>()->lines;
    REQUIRE(lines.size() == 3);
    CHECK(lines[1] == "early");
    CHECK(lines[2] == "late");
}

TEST_CASE("order: problems reports a constraint on an unknown system") {
    Schedule schedule;
    schedule.add_system(Stage::Update, nothing, "a", Order::after({"ghost"}));
    schedule.add_system(Stage::Update, nothing, "b", Order::before({"phantom"}));
    const auto problems = schedule.problems();
    REQUIRE(problems.size() == 2);
    CHECK(problems[0].find("'ghost'") != std::string::npos);
    CHECK(problems[1].find("'phantom'") != std::string::npos);
    CHECK(problems[0].find("Update") != std::string::npos);
}

TEST_CASE("order: constraints do not reach across stages") {
    Schedule schedule;
    schedule.add_system(Stage::PreUpdate, nothing, "input");
    schedule.add_system(Stage::Update, nothing, "movement", Order::after({"input"}));
    CHECK(schedule.problems().size() == 1);
}

TEST_CASE("order: problems reports duplicate names") {
    Schedule schedule;
    schedule.add_system(Stage::Update, nothing, "same");
    schedule.add_system(Stage::Update, nothing, "same");
    const auto problems = schedule.problems();
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].find("'same'") != std::string::npos);
}

TEST_CASE("order: problems reports a cycle, and every system still runs once") {
    App app;
    struct Counter {
        int runs = 0;
    };
    app.insert_resource(Counter{});
    const auto count = [](World& world) { ++world.resource<Counter>()->runs; };
    app.add_system(Stage::Update, count, "a", Order::after({"b"}));
    app.add_system(Stage::Update, count, "b", Order::after({"a"}));
    app.add_system(Stage::Update, count, "free");

    const auto problems = app.schedule().problems();
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].find("cycle") != std::string::npos);
    CHECK(problems[0].find("'a'") != std::string::npos);
    CHECK(problems[0].find("'b'") != std::string::npos);
    CHECK(problems[0].find("'free'") == std::string::npos);

    app.update();
    CHECK(app.world().resource<Counter>()->runs == 3);
}

TEST_CASE("order: a system that waits on itself is a cycle") {
    Schedule schedule;
    schedule.add_system(Stage::Update, nothing, "loop", Order::after({"loop"}));
    CHECK(schedule.problems().size() == 1);
}

TEST_CASE("order: between sets both sides at once") {
    Schedule schedule;
    schedule.add_system(Stage::Update, nothing, "physics", Order::between({"input"}, {"render"}));
    schedule.add_system(Stage::Update, nothing, "render");
    schedule.add_system(Stage::Update, nothing, "input");
    CHECK(names(schedule) == (std::vector<std::string>{"input", "physics", "render"}));
}

TEST_CASE("order: the same constraint stated twice is harmless") {
    Schedule schedule;
    schedule.add_system(Stage::Update, nothing, "b", Order::after({"a", "a"}));
    schedule.add_system(Stage::Update, nothing, "a");
    CHECK(names(schedule) == (std::vector<std::string>{"a", "b"}));
    CHECK(schedule.problems().empty());
}

TEST_CASE("order: while the game is paused, only the systems that run paused go on") {
    Schedule schedule;
    int moved = 0;
    int menus = 0;
    int drawn = 0;
    schedule.add_system(Stage::FixedUpdate, [&moved](World&) { moved += 1; }, "move");
    schedule.add_system(Stage::FixedUpdate, [&menus](World&) { menus += 1; }, "menu",
                        Order{}.while_paused());
    schedule.add_system(Stage::Update, [&drawn](World&) { drawn += 1; }, "draw");
    World world;
    world.insert_resource(ge::app::Time{});
    schedule.run_stage(Stage::FixedUpdate, world);
    world.resource<ge::app::Time>()->paused = true;
    schedule.run_stage(Stage::FixedUpdate, world);
    schedule.run_stage(Stage::Update, world);
    CHECK(moved == 1);
    CHECK(menus == 2);
    CHECK(drawn == 1);
}
