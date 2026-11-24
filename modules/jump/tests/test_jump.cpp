#include "ge/app/app.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/mod/collision2d/collision2d.gen.hpp"
#include "ge/mod/gravity2d/gravity2d.gen.hpp"
#include "ge/mod/jump/jump.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

namespace collision2d = ge::mod::collision2d;
namespace gravity2d = ge::mod::gravity2d;
namespace jump = ge::mod::jump;
namespace transform2d = ge::mod::transform2d;

/// An entity with a Position and a box: solid for a wall, a body otherwise.
ge::ecs::Entity spawn_box(ge::app::App& app, float x, float y, collision2d::Collider collider) {
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{x, y}));
    CHECK(app.world().insert(entity, collider));
    return entity;
}

/// A 10 by 10 jumper at (`x`, `y`), pulled down by `fall`.
ge::ecs::Entity spawn_jumper(ge::app::App& app, float x, float y, const jump::Jumper& jumper,
                             float fall) {
    const ge::ecs::Entity hero =
        spawn_box(app, x, y, collision2d::Collider{10.0F, 10.0F, 1U, 0xFFFFFFFFU});
    CHECK(app.world().insert(hero, transform2d::Velocity{}));
    CHECK(app.world().insert(hero, collision2d::Body{}));
    CHECK(app.world().insert(hero, collision2d::Blocked{}));
    CHECK(app.world().insert(hero, gravity2d::Gravity{0.0F, fall, 0.0F}));
    CHECK(app.world().insert(hero, jumper));
    return hero;
}

/// A floor (y 90..110) and a 10 by 10 jumper resting on it at y 85, at 60 ticks a second.
struct Level {
    ge::app::App app;
    ge::ecs::Entity floor;
    ge::ecs::Entity hero;
    std::uint64_t tick = 0;

    explicit Level(const jump::Jumper& jumper = {}, float fall = 1000.0F) {
        transform2d::install(app);
        collision2d::install(app);
        gravity2d::install(app);
        jump::install(app);
        CHECK(app.module_problems().empty());
        CHECK(app.schedule().problems().empty());
        floor = spawn_box(app, 0.0F, 100.0F,
                          collision2d::Collider{200.0F, 20.0F, 1U, 0xFFFFFFFFU, true});
        hero = spawn_jumper(app, 0.0F, 85.0F, jumper, fall);
    }

    void run(std::uint64_t ticks) {
        app.run_frames(static_cast<std::uint32_t>(ticks));
        tick += ticks;
    }

    /// Holds `action` down for `ticks` ticks, starting with the next one.
    void hold(std::uint64_t ticks, const char* action = "jump") {
        auto* input = app.world().resource<ge::app::ScriptedInput>();
        input->add(tick, action, 1.0F);
        input->add(tick + ticks, action, 0.0F);
    }

    [[nodiscard]] float y() const { return app.world().get<transform2d::Position>(hero)->y; }

    /// Resting on the floor (to rounding: the push out of the floor is a float subtraction).
    [[nodiscard]] bool on_floor() const { return std::abs(y() - 85.0F) < 0.01F; }

    [[nodiscard]] std::uint32_t jumps() const { return app.world().get<jump::Jumper>(hero)->jumps; }

    /// The highest point (smallest y) of the hero over the next `ticks` ticks.
    float top_over(std::uint64_t ticks) {
        float top = y();
        for (std::uint64_t i = 0; i < ticks; ++i) {
            run(1);
            top = std::min(top, y());
        }
        return top;
    }
};

} // namespace

TEST_CASE("jump: a press on the ground jumps, a press in the air does not") {
    Level level;
    level.run(3); // settles on the floor
    CHECK(level.on_floor());
    level.hold(1);
    level.run(1);
    CHECK(level.jumps() == 1U);
    CHECK(level.app.world().get<transform2d::Velocity>(level.hero)->y < 0.0F);
    level.run(10);
    CHECK(level.y() < 70.0F);
    level.hold(1); // in the air: nothing
    level.run(2);
    CHECK(level.jumps() == 1U);
    level.run(120); // back on the floor
    CHECK(level.on_floor());
    level.hold(1);
    level.run(1);
    CHECK(level.jumps() == 2U);
}

TEST_CASE("jump: the action can be named, and holding it does not jump again") {
    jump::Jumper jumper;
    jumper.action = 0;
    Level level(jumper);
    level.app.world().insert_resource(ge::app::ActionNames{{"leap"}});
    level.run(3);
    level.hold(1); // "jump" is not this jumper's action
    level.run(2);
    CHECK(level.jumps() == 0U);
    level.hold(200, "leap"); // held through the landing
    level.run(150);
    CHECK(level.jumps() == 1U);
}

TEST_CASE("jump: a short grace after leaving the ground, not a long one") {
    for (const std::uint64_t wait : {3U, 10U}) {
        Level level;
        level.run(3);
        level.app.world().get<transform2d::Position>(level.floor)->y = 10000.0F; // gone
        level.run(wait); // 3 ticks are 0.05 s, 10 are 0.17 s: coyote_time is 0.1 s
        level.hold(1);
        level.run(1);
        CHECK(level.jumps() == (wait == 3U ? 1U : 0U));
    }
}

TEST_CASE("jump: a press just before landing jumps on landing, an earlier one does not") {
    for (const std::uint64_t early : {2U, 6U}) {
        Level level({}, 0.0F); // no gravity: the hero falls 1 unit a tick
        level.app.world().get<transform2d::Position>(level.hero)->y = 75.0F;
        level.app.world().get<transform2d::Velocity>(level.hero)->y = 60.0F;
        // The hero lands during tick 10 and knows it from tick 11.
        level.run(early);
        level.hold(1);
        level.run(12 - early);
        // Pressed at tick 6: 5 ticks (0.08 s) before tick 11, within buffer_time (0.1 s).
        CHECK(level.jumps() == (early == 6U ? 1U : 0U));
    }
}

TEST_CASE("jump: a short press makes a lower jump than a long one") {
    Level tap;
    tap.run(3);
    tap.hold(1);
    const float tap_top = tap.top_over(60);
    Level held;
    held.run(3);
    held.hold(60);
    const float held_top = held.top_over(60);
    CHECK(held_top < tap_top - 20.0F);

    jump::Jumper uncut;
    uncut.cut = 1.0F;
    Level full(uncut);
    full.run(3);
    full.hold(1);
    CHECK(full.top_over(60) == held_top);
}

TEST_CASE("jump: air jumps are spent in the air and come back on the ground") {
    jump::Jumper jumper;
    jumper.air_jumps = 1;
    Level level(jumper);
    level.run(3);
    level.hold(1);
    level.run(10);
    level.hold(1);
    level.run(10);
    CHECK(level.jumps() == 2U); // one from the ground, one in the air
    level.hold(1);
    level.run(10);
    CHECK(level.jumps() == 2U); // no air jump left
    level.run(200);
    level.hold(1);
    level.run(10);
    level.hold(1);
    level.run(1);
    CHECK(level.jumps() == 4U);
}

TEST_CASE("jump: a Disabled jumper does not jump, and jumps again once enabled") {
    Level level;
    level.run(3);
    CHECK(level.app.world().insert(level.hero, transform2d::Disabled{}));
    level.hold(5);
    level.run(5);
    CHECK(level.jumps() == 0U);
    CHECK(level.on_floor());
    CHECK(level.app.world().remove<transform2d::Disabled>(level.hero));
    level.run(1); // the earlier press is let go
    level.hold(1);
    level.run(1);
    CHECK(level.jumps() == 1U);
}
