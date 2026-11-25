#include "ge/app/app.hpp"
#include "ge/app/fields.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/mod/checkpoint/checkpoint.gen.hpp"
#include "ge/mod/tag/tag.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace {

namespace checkpoint = ge::mod::checkpoint;
namespace tag = ge::mod::tag;
namespace transform2d = ge::mod::transform2d;

constexpr std::uint32_t save_action = 0U;
constexpr std::uint32_t load_action = 1U;
constexpr std::uint32_t level_tag = 2U;

struct Run {
    ge::app::App app;
    std::uint64_t tick = 0;

    Run() {
        transform2d::install(app);
        tag::install(app);
        checkpoint::install(app);
        CHECK(app.module_problems().empty());
        CHECK(app.schedule().problems().empty());
        app.world().insert_resource(ge::app::ActionNames{{"save", "load"}});
    }
    void tap(const char* action) {
        auto* input = app.world().resource<ge::app::ScriptedInput>();
        input->add(tick, action, 1.0F);
        input->add(tick + 1, action, 0.0F);
        run(2);
    }
    void run(std::uint64_t ticks) {
        app.run_frames(static_cast<std::uint32_t>(ticks));
        tick += ticks;
    }
    std::uint32_t field(const std::string& name) {
        const auto* table = app.world().resource<ge::app::FieldTable>();
        const auto found = std::find_if(table->entries.begin(), table->entries.end(),
                                        [&name](const auto& entry) { return entry.name == name; });
        CHECK(found != table->entries.end());
        return static_cast<std::uint32_t>(found - table->entries.begin());
    }
};

} // namespace

TEST_CASE("checkpoint: the numbers of the Saved entities are kept at the start and put back") {
    Run run;
    const ge::ecs::Entity point = run.app.world().spawn();
    CHECK(run.app.world().insert(point, checkpoint::Checkpoint{save_action, load_action}));
    const ge::ecs::Entity hero = run.app.world().spawn();
    CHECK(run.app.world().insert(hero, transform2d::Position{10.0F, 20.0F}));
    CHECK(run.app.world().insert(hero, transform2d::Velocity{3.0F, 0.0F}));
    CHECK(run.app.world().insert(hero, checkpoint::Saved{}));
    const ge::ecs::Entity other = run.app.world().spawn();
    CHECK(run.app.world().insert(other, transform2d::Position{5.0F, 5.0F}));
    run.run(1);
    CHECK(run.app.world().get<checkpoint::Checkpoint>(point)->saves == 1U);
    run.app.world().get<transform2d::Position>(hero)->x = 500.0F;
    run.app.world().get<transform2d::Velocity>(hero)->x = -8.0F;
    run.app.world().get<transform2d::Position>(other)->x = 77.0F;
    run.tap("load");
    // Back at 10, and its speed back too: it has moved on by a tick or two since.
    CHECK(std::abs(run.app.world().get<transform2d::Position>(hero)->x - 10.0F) < 0.2F);
    CHECK(run.app.world().get<transform2d::Velocity>(hero)->x == 3.0F);
    CHECK(run.app.world().get<transform2d::Position>(other)->x == 77.0F); // not Saved
    run.app.world().get<transform2d::Position>(hero)->y = 99.0F;
    run.tap("save");
    run.app.world().get<transform2d::Position>(hero)->y = 0.0F;
    run.tap("load");
    CHECK(run.app.world().get<transform2d::Position>(hero)->y == 99.0F);
    CHECK(run.app.world().get<checkpoint::Checkpoint>(point)->loads == 2U);
}

TEST_CASE("checkpoint: a change of the watched number keeps the numbers again") {
    Run run;
    const ge::ecs::Entity level = run.app.world().spawn();
    CHECK(run.app.world().insert(level, transform2d::Position{1.0F, 0.0F}));
    CHECK(run.app.world().insert(level, tag::Tag{level_tag}));
    checkpoint::Checkpoint made;
    made.load = load_action;
    made.watch = run.field("transform2d.Position.x");
    made.source = level_tag;
    const ge::ecs::Entity point = run.app.world().spawn();
    CHECK(run.app.world().insert(point, made));
    const ge::ecs::Entity hero = run.app.world().spawn();
    CHECK(run.app.world().insert(hero, transform2d::Position{0.0F, 0.0F}));
    CHECK(run.app.world().insert(hero, checkpoint::Saved{}));
    run.run(1);
    run.app.world().get<transform2d::Position>(hero)->x = 40.0F;
    run.app.world().get<transform2d::Position>(level)->x = 2.0F; // the next step
    run.run(1);
    CHECK(run.app.world().get<checkpoint::Checkpoint>(point)->saves == 2U);
    run.app.world().get<transform2d::Position>(hero)->x = 90.0F;
    run.tap("load");
    CHECK(run.app.world().get<transform2d::Position>(hero)->x == 40.0F);
}
