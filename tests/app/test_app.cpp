#include "ge/app/app.hpp"

#include "../support/mini_test.hpp"

#include <string>
#include <vector>

using ge::app::App;
using ge::app::Stage;
using ge::ecs::World;

namespace {

struct Log {
    std::vector<std::string> lines;
};

void write(World& world, const char* line) {
    world.resource<Log>()->lines.emplace_back(line);
}

App app_with_log() {
    App app;
    app.insert_resource(Log{});
    return app;
}

struct Counter {
    int value = 0;
};

struct CounterPlugin {
    void build(App& app) const {
        app.insert_resource(Counter{});
        app.add_system(
            Stage::Update, [](World& world) { ++world.resource<Counter>()->value; }, "count");
    }
};

} // namespace

TEST_CASE("app: startup runs once, before the first frame") {
    App app = app_with_log();
    app.add_system(Stage::Update, [](World& world) { write(world, "update"); });
    app.add_system(Stage::Startup, [](World& world) { write(world, "startup"); });
    app.run_frames(2);
    const auto& lines = app.world().resource<Log>()->lines;
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "startup");
    CHECK(lines[1] == "update");
    CHECK(lines[2] == "update");
}

TEST_CASE("app: stages run in frame order whatever the registration order") {
    App app = app_with_log();
    app.add_system(Stage::PostUpdate, [](World& world) { write(world, "post"); })
        .add_system(Stage::Update, [](World& world) { write(world, "update"); })
        .add_system(Stage::PreUpdate, [](World& world) { write(world, "pre"); });
    app.update();
    const auto& lines = app.world().resource<Log>()->lines;
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "pre");
    CHECK(lines[1] == "update");
    CHECK(lines[2] == "post");
}

TEST_CASE("app: systems of a stage run in registration order") {
    App app = app_with_log();
    app.add_system(Stage::Update, [](World& world) { write(world, "first"); });
    app.add_system(Stage::Update, [](World& world) { write(world, "second"); });
    app.update();
    const auto& lines = app.world().resource<Log>()->lines;
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "first");
    CHECK(lines[1] == "second");
}

TEST_CASE("app: a plugin object registers resources and systems") {
    App app;
    app.add_plugin(CounterPlugin{});
    app.run_frames(3);
    REQUIRE(app.world().resource<Counter>() != nullptr);
    CHECK(app.world().resource<Counter>()->value == 3);
}

TEST_CASE("app: a lambda is a plugin") {
    App app = app_with_log();
    const auto plugin = [](App& target) {
        target.add_system(Stage::Update, [](World& world) { write(world, "from plugin"); });
    };
    app.add_plugin(plugin);
    app.update();
    CHECK(app.world().resource<Log>()->lines.size() == 1);
}

TEST_CASE("app: frame_count tracks updates") {
    App app;
    CHECK(app.frame_count() == 0);
    app.run_frames(5);
    app.update();
    CHECK(app.frame_count() == 6);
}

TEST_CASE("app: systems get default or explicit names") {
    App app;
    app.add_system(Stage::Update, [](World&) {});
    app.add_system(Stage::Update, [](World&) {}, "physics");
    const auto names = app.schedule().system_names(Stage::Update);
    REQUIRE(names.size() == 2);
    CHECK(names[0] == "system_0");
    CHECK(names[1] == "physics");
    CHECK(app.schedule().system_count(Stage::Update) == 2);
    CHECK(app.schedule().system_count(Stage::Startup) == 0);
}

TEST_CASE("app: entities spawned before running are visible to systems") {
    App app;
    app.world().spawn_with(Counter{10});
    app.add_system(Stage::Update,
                   [](World& world) { world.each<Counter>([](auto, Counter& c) { ++c.value; }); });
    app.update();
    int total = 0;
    app.world().each<const Counter>([&total](auto, const Counter& c) { total += c.value; });
    CHECK(total == 11);
}
