#include "ge/app/app.hpp"
#include "ge/mod/attractor2d/attractor2d.gen.hpp"
#include "ge/mod/collision2d/collision2d.gen.hpp"
#include "ge/mod/reserve/reserve.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

#include <cmath>
#include <cstdint>

namespace {

namespace attractor2d = ge::mod::attractor2d;
namespace collision2d = ge::mod::collision2d;
namespace reserve = ge::mod::reserve;
namespace transform2d = ge::mod::transform2d;

constexpr std::uint32_t stone = 0U;
constexpr std::uint32_t coal = 1U;

ge::app::App game() {
    ge::app::App app;
    transform2d::install(app);
    collision2d::install(app);
    reserve::install(app);
    attractor2d::install(app);
    CHECK(app.module_problems().empty());
    CHECK(app.schedule().problems().empty());
    return app;
}

ge::ecs::Entity well(ge::app::App& app, const attractor2d::Attractor& made) {
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{0.0F, 0.0F}));
    CHECK(app.world().insert(entity, made));
    return entity;
}

ge::ecs::Entity thing(ge::app::App& app, float x, std::uint32_t layer, float mass,
                      std::uint32_t kind, float amount) {
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{x, 0.0F}));
    CHECK(app.world().insert(entity, transform2d::Velocity{}));
    CHECK(app.world().insert(entity, collision2d::Collider{4.0F, 4.0F, layer}));
    CHECK(app.world().insert(entity, attractor2d::Absorbable{mass}));
    CHECK(app.world().insert(entity, reserve::Yield{kind, amount}));
    return entity;
}

ge::ecs::Entity stock(ge::app::App& app, std::uint32_t kind) {
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, reserve::Reserve{kind, 0.0F, 100.0F}));
    return entity;
}

float amount(ge::app::App& app, ge::ecs::Entity stocked) {
    return app.world().get<reserve::Reserve>(stocked)->amount;
}

} // namespace

TEST_CASE("attractor2d: a well grows while it works, pulls what is near, and shrinks after") {
    ge::app::App app = game();
    attractor2d::Attractor made;
    made.start_radius = 20.0F;
    made.grow = 60.0F;
    made.max_radius = 40.0F;
    made.shrink = 120.0F;
    made.pulls = 1U;
    const ge::ecs::Entity hole = well(app, made);
    const ge::ecs::Entity near = thing(app, 30.0F, 1U, 1000.0F, stone, 1.0F);
    const ge::ecs::Entity spared = thing(app, -30.0F, 2U, 1000.0F, stone, 1.0F); // not in pulls
    app.run_frames(1);
    // It starts at its start radius, then grows a unit a tick.
    CHECK(app.world().get<attractor2d::Attractor>(hole)->radius == 20.0F);
    app.run_frames(1);
    CHECK(std::abs(app.world().get<attractor2d::Attractor>(hole)->radius - 21.0F) < 0.01F);
    app.run_frames(30); // a second: up to its largest, 40
    CHECK(app.world().get<attractor2d::Attractor>(hole)->radius == 40.0F);
    CHECK(app.world().get<transform2d::Velocity>(near)->x < 0.0F); // towards the well
    CHECK(app.world().get<transform2d::Velocity>(spared)->x == 0.0F);
    CHECK(app.world().insert(hole, transform2d::Disabled{}));
    app.run_frames(10);
    CHECK(std::abs(app.world().get<attractor2d::Attractor>(hole)->radius - 20.0F) < 0.01F);
    app.run_frames(20);
    CHECK(app.world().get<attractor2d::Attractor>(hole)->radius == 0.0F);
}

TEST_CASE("attractor2d: what reaches the heart is swallowed and turned into the convert kind") {
    ge::app::App app = game();
    const ge::ecs::Entity stones = stock(app, stone);
    const ge::ecs::Entity coals = stock(app, coal);
    attractor2d::Attractor made;
    made.start_radius = 30.0F;
    made.grow = 0.0F;
    made.max_radius = 30.0F;
    made.convert = stone;
    const ge::ecs::Entity hole = well(app, made);
    const ge::ecs::Entity pebble = thing(app, 5.0F, 1U, 5.0F, coal, 7.0F); // in the heart (12)
    const ge::ecs::Entity boulder = thing(app, 6.0F, 1U, 60.0F, coal, 7.0F); // too heavy
    const ge::ecs::Entity far = thing(app, 25.0F, 1U, 1.0F, coal, 7.0F); // outside the heart
    CHECK(app.world().insert(far, transform2d::Disabled{}));
    app.world().get<transform2d::Velocity>(far)->x = 0.0F;
    app.run_frames(1);
    CHECK(!app.world().is_alive(pebble));
    CHECK(app.world().is_alive(boulder));
    CHECK(app.world().get<attractor2d::Attractor>(hole)->swallowed == 1U);
    CHECK(amount(app, stones) == 7.0F); // the coal became stone
    CHECK(amount(app, coals) == 0.0F);
}

TEST_CASE("attractor2d: a deferred well gives each kind its own, once it stops") {
    ge::app::App app = game();
    const ge::ecs::Entity stones = stock(app, stone);
    const ge::ecs::Entity coals = stock(app, coal);
    attractor2d::Attractor made;
    made.start_radius = 30.0F;
    made.max_radius = 30.0F;
    made.deferred = true;
    const ge::ecs::Entity bubble = well(app, made);
    thing(app, 2.0F, 1U, 0.0F, coal, 4.0F);
    thing(app, -2.0F, 1U, 0.0F, stone, 3.0F);
    app.run_frames(2);
    CHECK(app.world().get<attractor2d::Attractor>(bubble)->pending == 7.0F);
    CHECK(amount(app, coals) == 0.0F);
    CHECK(app.world().insert(bubble, transform2d::Disabled{}));
    app.run_frames(1);
    CHECK(amount(app, coals) == 4.0F);
    CHECK(amount(app, stones) == 3.0F);
    CHECK(app.world().get<attractor2d::Attractor>(bubble)->pending == 0.0F);
}
