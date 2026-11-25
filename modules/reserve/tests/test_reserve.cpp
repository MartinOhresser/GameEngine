#include "ge/app/app.hpp"
#include "ge/app/fields.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/mod/reserve/reserve.gen.hpp"
#include "ge/mod/reserve/stock.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace {

namespace reserve = ge::mod::reserve;
namespace transform2d = ge::mod::transform2d;

constexpr std::uint32_t water = 0U;
constexpr std::uint32_t stone = 1U;

ge::app::App game() {
    ge::app::App app;
    transform2d::install(app);
    reserve::install(app);
    CHECK(app.module_problems().empty());
    CHECK(app.schedule().problems().empty());
    app.world().insert_resource(ge::app::ActionNames{{"dry"}});
    return app;
}

std::uint32_t field_index(const ge::app::App& app, const std::string& name) {
    const auto* table = app.world().resource<ge::app::FieldTable>();
    const auto found = std::find_if(table->entries.begin(), table->entries.end(),
                                    [&name](const auto& entry) { return entry.name == name; });
    CHECK(found != table->entries.end());
    return static_cast<std::uint32_t>(found - table->entries.begin());
}

ge::ecs::Entity stock(ge::app::App& app, const reserve::Reserve& made) {
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, made));
    return entity;
}

bool near(float a, float b) { return std::abs(a - b) < 0.01F; }

} // namespace

TEST_CASE("reserve: a reserve drains by itself down to 0 and presses its action then") {
    ge::app::App app = game();
    // 30 a second: from 20 to 10 in 20 ticks, to 0 in 40.
    const ge::ecs::Entity tank =
        stock(app, reserve::Reserve{water, 20.0F, 40.0F, 30.0F, 0.0F, 0U});
    app.run_frames(20);
    CHECK(near(app.world().get<reserve::Reserve>(tank)->amount, 10.0F));
    CHECK(near(app.world().get<reserve::Reserve>(tank)->fill, 0.25F));
    bool pressed = false;
    for (int tick = 0; tick < 30; tick += 1) {
        app.run_frames(1);
        pressed = pressed || app.world().resource<ge::app::Actions>()->pressed("dry");
    }
    CHECK(pressed);
    CHECK(app.world().get<reserve::Reserve>(tank)->amount == 0.0F);
}

TEST_CASE("reserve: a floor stops the drain; a Disabled reserve does not drain") {
    ge::app::App spared = game();
    const ge::ecs::Entity kept =
        stock(spared, reserve::Reserve{water, 20.0F, 40.0F, 30.0F, 15.0F});
    spared.run_frames(60);
    CHECK(near(spared.world().get<reserve::Reserve>(kept)->amount, 15.0F));

    ge::app::App app = game();
    const ge::ecs::Entity tank =
        stock(app, reserve::Reserve{water, 50.0F, 100.0F, 60.0F, 10.0F});
    CHECK(app.world().insert(tank, transform2d::Disabled{}));
    app.run_frames(30);
    CHECK(app.world().get<reserve::Reserve>(tank)->amount == 50.0F);
}

TEST_CASE("reserve: spending respects the floor, giving the max") {
    ge::app::App app = game();
    stock(app, reserve::Reserve{water, 50.0F, 100.0F, 0.0F, 10.0F});
    CHECK(reserve::can_spend(app.world(), water, 40.0F));
    CHECK(!reserve::can_spend(app.world(), water, 41.0F));
    CHECK(!reserve::spend(app.world(), water, 41.0F));
    CHECK(reserve::spend(app.world(), water, 40.0F));
    CHECK(near(reserve::drain(app.world(), water, 5.0F), 0.0F)); // already at its floor
    CHECK(near(reserve::give(app.world(), water, 500.0F), 90.0F)); // up to its max
    CHECK(reserve::spend(app.world(), stone, 1000.0F)); // a kind nobody keeps costs nothing
    CHECK(reserve::give(app.world(), stone, 5.0F) == 0.0F);
}

TEST_CASE("reserve: a Drain follows a number, a Gain counts what was gathered") {
    ge::app::App app = game();
    const ge::ecs::Entity tank = stock(app, reserve::Reserve{water, 100.0F, 100.0F});
    const ge::ecs::Entity rocks = stock(app, reserve::Reserve{stone, 0.0F, 100.0F});
    const ge::ecs::Entity walker = app.world().spawn();
    CHECK(app.world().insert(walker, transform2d::Velocity{-120.0F, 0.0F}));
    // 1 a second, plus 0.05 a second for each unit of speed: 7 a second at -120.
    CHECK(app.world().insert(
        walker, reserve::Drain{water, 1.0F, field_index(app, "transform2d.Velocity.x"), 0.05F}));
    // 0.5 stone for each unit of y of the Velocity, every tick.
    CHECK(app.world().insert(
        walker, reserve::Gain{stone, field_index(app, "transform2d.Velocity.y"), 0.5F}));
    app.run_frames(60);
    CHECK(near(app.world().get<reserve::Reserve>(tank)->amount, 93.0F));
    CHECK(app.world().get<reserve::Reserve>(rocks)->amount == 0.0F);
    app.world().get<transform2d::Velocity>(walker)->y = -4.0F;
    app.run_frames(3);
    CHECK(near(app.world().get<reserve::Reserve>(rocks)->amount, 6.0F));
    CHECK(app.world().insert(walker, transform2d::Disabled{}));
    const float before = app.world().get<reserve::Reserve>(tank)->amount;
    app.run_frames(10);
    CHECK(app.world().get<reserve::Reserve>(tank)->amount == before);
}
