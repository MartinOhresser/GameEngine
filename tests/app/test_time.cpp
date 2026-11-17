#include "ge/app/app.hpp"
#include "ge/core/random.hpp"

#include "../support/mini_test.hpp"

#include <chrono>
#include <string>
#include <vector>

using ge::Random;
using ge::app::App;
using ge::app::Stage;
using ge::app::Time;
using ge::ecs::World;
using std::chrono::milliseconds;
using std::chrono::seconds;

namespace {

struct Log {
    std::vector<std::string> lines;
};

struct Counter {
    int value = 0;
};

App app_counting_fixed_ticks() {
    App app;
    app.insert_resource(Counter{});
    app.add_system(Stage::FixedUpdate, [](World& world) { ++world.resource<Counter>()->value; });
    return app;
}

int ticks_run(const App& app) {
    return app.world().resource<Counter>()->value;
}

} // namespace

TEST_CASE("time: the app starts with a 60 Hz clock") {
    App app;
    const Time* time = app.world().resource<Time>();
    REQUIRE(time != nullptr);
    CHECK(time->tick == 0);
    CHECK(time->tick_rate == 60);
    CHECK(time->fixed_delta > 0.0166F);
    CHECK(time->fixed_delta < 0.0167F);
}

TEST_CASE("time: update runs exactly one fixed tick") {
    App app = app_counting_fixed_ticks();
    app.run_frames(5);
    CHECK(ticks_run(app) == 5);
    CHECK(app.tick_count() == 5);
    CHECK(app.frame_count() == 5);
}

TEST_CASE("time: advance accumulates real time into whole ticks") {
    App app = app_counting_fixed_ticks();
    app.advance(milliseconds(10));
    CHECK(ticks_run(app) == 0);
    app.advance(milliseconds(10)); // 20 ms in total: one 16.67 ms tick fits
    CHECK(ticks_run(app) == 1);
    app.advance(milliseconds(50)); // 3.3 ms left over plus 50 ms: three more ticks fit
    CHECK(ticks_run(app) == 4);
    CHECK(app.tick_count() == 4);
}

TEST_CASE("time: a slow frame is capped and the excess is dropped") {
    App app = app_counting_fixed_ticks();
    app.advance(seconds(10));
    CHECK(ticks_run(app) == 8);
    app.advance(milliseconds(0));
    CHECK(ticks_run(app) == 8);

    App tighter = app_counting_fixed_ticks();
    tighter.set_max_catch_up_ticks(2);
    tighter.advance(seconds(1));
    CHECK(ticks_run(tighter) == 2);
}

TEST_CASE("time: a negative delta counts as no time") {
    App app = app_counting_fixed_ticks();
    app.advance(milliseconds(-5));
    CHECK(ticks_run(app) == 0);
}

TEST_CASE("time: the tick rate can be changed before running") {
    App app = app_counting_fixed_ticks();
    app.set_tick_rate(30);
    const Time* time = app.world().resource<Time>();
    REQUIRE(time != nullptr);
    CHECK(time->tick_rate == 30);
    CHECK(time->fixed_delta > 0.0333F);
    CHECK(time->fixed_delta < 0.0334F);

    app.advance(milliseconds(34));
    CHECK(ticks_run(app) == 1);
}

TEST_CASE("time: frame_delta and alpha describe the frame for presentation") {
    App app;
    app.advance(milliseconds(25)); // one tick (16.67 ms) plus about half of the next
    const Time* time = app.world().resource<Time>();
    REQUIRE(time != nullptr);
    CHECK(time->tick == 1);
    CHECK(time->frame_delta > 0.0249F);
    CHECK(time->frame_delta < 0.0251F);
    CHECK(time->alpha > 0.45F);
    CHECK(time->alpha < 0.55F);
}

TEST_CASE("time: stages run in order, with the fixed ticks between pre and post") {
    App app;
    app.insert_resource(Log{});
    const auto record = [](const char* text) {
        return [text](World& world) { world.resource<Log>()->lines.emplace_back(text); };
    };
    app.add_system(Stage::PostUpdate, record("post"));
    app.add_system(Stage::Update, record("update"));
    app.add_system(Stage::FixedUpdate, record("fixed"));
    app.add_system(Stage::PreUpdate, record("pre"));
    app.add_system(Stage::Startup, record("startup"));

    app.advance(milliseconds(40)); // two ticks

    const auto& lines = app.world().resource<Log>()->lines;
    REQUIRE(lines.size() == 6);
    CHECK(lines[0] == "startup");
    CHECK(lines[1] == "pre");
    CHECK(lines[2] == "fixed");
    CHECK(lines[3] == "fixed");
    CHECK(lines[4] == "update");
    CHECK(lines[5] == "post");
}

TEST_CASE("time: stage names are readable") {
    CHECK(ge::app::to_string(Stage::Startup) == "Startup");
    CHECK(ge::app::to_string(Stage::PreUpdate) == "PreUpdate");
    CHECK(ge::app::to_string(Stage::FixedUpdate) == "FixedUpdate");
    CHECK(ge::app::to_string(Stage::Update) == "Update");
    CHECK(ge::app::to_string(Stage::PostUpdate) == "PostUpdate");
}

namespace {

// A tiny simulation that draws from the world's generator on every tick.
int simulate(std::uint64_t seed, std::uint32_t ticks) {
    App app;
    app.insert_resource(Random(seed));
    app.insert_resource(Counter{});
    app.add_system(Stage::FixedUpdate, [](World& world) {
        auto* random = world.resource<Random>();
        world.resource<Counter>()->value += static_cast<int>(random->below(100));
    });
    app.run_frames(ticks);
    return app.world().resource<Counter>()->value;
}

} // namespace

TEST_CASE("time: the same seed replays the same simulation") {
    CHECK(simulate(7, 200) == simulate(7, 200));
    CHECK(simulate(7, 200) != simulate(8, 200));
}

TEST_CASE("time: the app provides a generator resource") {
    App app;
    CHECK(app.world().resource<Random>() != nullptr);
}
