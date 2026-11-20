#include "ge/app/app.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/mod/health/damage.hpp"
#include "ge/mod/health/health.gen.hpp"

#include "mini_test.hpp"

namespace {

namespace health = ge::mod::health;
using ge::ecs::Entity;

ge::app::App game() {
    ge::app::App app;
    health::install(app);
    CHECK(app.module_problems().empty());
    CHECK(app.schedule().problems().empty());
    app.world().insert_resource(ge::app::ActionNames{{"died", "other"}});
    return app;
}

Entity with_health(ge::app::App& app, std::int32_t amount, bool despawn = true,
                   std::uint32_t action = 0) {
    const Entity entity = app.world().spawn();
    health::Health life;
    life.amount = amount;
    life.max = amount;
    life.despawn_on_zero = despawn;
    life.on_zero = action;
    CHECK(app.world().insert(entity, life));
    return entity;
}

void hurt(ge::app::App& app, Entity entity, std::int32_t amount) {
    CHECK(app.world().insert(entity, health::Damage{amount}));
}

} // namespace

TEST_CASE("health: damage lowers the health and is used up") {
    auto app = game();
    const Entity monster = with_health(app, 3);
    hurt(app, monster, 1);
    app.run_frames(1);
    CHECK(app.world().get<health::Health>(monster)->amount == 2);
    CHECK(!app.world().has<health::Damage>(monster));
    app.run_frames(3); // nothing is applied twice
    CHECK(app.world().get<health::Health>(monster)->amount == 2);
}

TEST_CASE("health: an entity at zero is dead, announced once, and destroyed if it says so") {
    auto app = game();
    const Entity monster = with_health(app, 2);
    const Entity player = with_health(app, 1, false, 0);
    hurt(app, monster, 5);
    hurt(app, player, 1);
    app.run_frames(1);
    CHECK(!app.world().is_alive(monster));
    REQUIRE(app.world().is_alive(player));
    CHECK(app.world().has<health::Dead>(player));
    CHECK(app.world().get<health::Health>(player)->amount == 0);
    app.run_frames(1); // an action is pressed from the tick after it is raised
    CHECK(app.world().resource<ge::app::Actions>()->pressed("died"));
    app.run_frames(1);
    hurt(app, player, 1); // a dead entity cannot die again
    app.run_frames(2);
    CHECK(!app.world().resource<ge::app::Actions>()->pressed("died"));
}

TEST_CASE("health: a negative damage heals, never above the maximum") {
    auto app = game();
    const Entity monster = with_health(app, 5);
    hurt(app, monster, 3);
    app.run_frames(1);
    hurt(app, monster, -1);
    app.run_frames(1);
    CHECK(app.world().get<health::Health>(monster)->amount == 3);
    hurt(app, monster, -10);
    app.run_frames(1);
    CHECK(app.world().get<health::Health>(monster)->amount == 5);
}

TEST_CASE("health: damage requests of one tick add up, from any source") {
    auto app = game();
    const Entity monster = with_health(app, 10);
    health::deal(app.world(), monster, 2);
    app.world().resource<ge::ecs::Commands>()->apply(app.world());
    health::deal(app.world(), monster, 3);
    app.run_frames(1);
    CHECK(app.world().get<health::Health>(monster)->amount == 5);
}

TEST_CASE("health: damage on something that has no health is ignored and removed") {
    auto app = game();
    const Entity rock = app.world().spawn();
    hurt(app, rock, 4);
    app.run_frames(1);
    CHECK(app.world().is_alive(rock));
    CHECK(!app.world().has<health::Damage>(rock));
}
