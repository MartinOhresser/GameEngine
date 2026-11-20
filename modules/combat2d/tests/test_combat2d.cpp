#include "ge/app/app.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/mod/collision2d/collision2d.gen.hpp"
#include "ge/mod/combat2d/combat2d.gen.hpp"
#include "ge/mod/health/health.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

#include <cmath>

namespace {

namespace collision2d = ge::mod::collision2d;
namespace combat2d = ge::mod::combat2d;
namespace health = ge::mod::health;
namespace transform2d = ge::mod::transform2d;

using ge::ecs::Entity;

constexpr std::uint32_t player_layer = 1U;
constexpr std::uint32_t monster_layer = 2U;
constexpr std::uint32_t shot_layer = 4U;

ge::app::App game() {
    ge::app::App app;
    transform2d::install(app);
    collision2d::install(app);
    health::install(app);
    combat2d::install(app);
    CHECK(app.module_problems().empty());
    CHECK(app.schedule().problems().empty());
    app.world().insert_resource(ge::app::ActionNames{{"died", "other"}});
    return app;
}

Entity spawn_box(ge::app::App& app, float x, float y, std::uint32_t layer) {
    const Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{x, y}));
    CHECK(app.world().insert(entity, collision2d::Collider{20.0F, 20.0F, layer, 0U, false}));
    return entity;
}

Entity spawn_target(ge::app::App& app, float x, float y, std::int32_t amount,
                    std::uint32_t layer = monster_layer) {
    const Entity entity = spawn_box(app, x, y, layer);
    health::Health life;
    life.amount = amount;
    life.max = amount;
    CHECK(app.world().insert(entity, life));
    return entity;
}

Entity spawn_shot(ge::app::App& app, float x, float y, combat2d::Projectile shot) {
    const Entity entity = spawn_box(app, x, y, shot_layer);
    CHECK(app.world().insert(entity, shot));
    return entity;
}

combat2d::Projectile damage_of(std::int32_t damage) {
    combat2d::Projectile shot;
    shot.damage = damage;
    return shot;
}

std::int32_t life_of(ge::app::App& app, Entity entity) {
    return app.world().get<health::Health>(entity)->amount;
}

} // namespace

TEST_CASE("combat2d: a projectile destroys a target with one life, and itself") {
    auto app = game();
    const Entity target = spawn_target(app, 100.0F, 100.0F, 1);
    const Entity shot = spawn_shot(app, 105.0F, 95.0F, damage_of(1));
    app.run_frames(1);
    CHECK(!app.world().is_alive(target));
    CHECK(!app.world().is_alive(shot));
}

TEST_CASE("combat2d: a target with several lives loses one per hit") {
    auto app = game();
    const Entity target = spawn_target(app, 100.0F, 100.0F, 3);
    const Entity first = spawn_shot(app, 100.0F, 100.0F, damage_of(1));
    app.run_frames(1);
    REQUIRE(app.world().is_alive(target));
    CHECK(life_of(app, target) == 2);
    CHECK(!app.world().is_alive(first));
    static_cast<void>(spawn_shot(app, 100.0F, 100.0F, damage_of(2)));
    app.run_frames(1);
    CHECK(!app.world().is_alive(target));
}

TEST_CASE("combat2d: a projectile that misses goes on, and hits only one target") {
    auto app = game();
    const Entity far = spawn_target(app, 300.0F, 300.0F, 1);
    const Entity near_a = spawn_target(app, 100.0F, 100.0F, 1);
    const Entity near_b = spawn_target(app, 105.0F, 100.0F, 1);
    const Entity shot = spawn_shot(app, 102.0F, 100.0F, damage_of(1));
    app.run_frames(1);
    CHECK(app.world().is_alive(far));
    CHECK(!app.world().is_alive(shot));
    CHECK(app.world().is_alive(near_a) != app.world().is_alive(near_b)); // exactly one died
}

TEST_CASE("combat2d: a projectile only hurts the layers it is for") {
    auto app = game();
    const Entity ally = spawn_target(app, 100.0F, 100.0F, 1, player_layer);
    combat2d::Projectile shot = damage_of(1);
    shot.layers = monster_layer;
    const Entity bullet = spawn_shot(app, 100.0F, 100.0F, shot);
    app.run_frames(2);
    CHECK(app.world().is_alive(ally));
    CHECK(app.world().is_alive(bullet)); // it goes on
}

TEST_CASE("combat2d: a splash hurts everything around the hit, and nothing farther") {
    auto app = game();
    const Entity centre = spawn_target(app, 100.0F, 100.0F, 5);
    const Entity near = spawn_target(app, 140.0F, 100.0F, 5);
    const Entity far = spawn_target(app, 200.0F, 100.0F, 5);
    combat2d::Projectile shell = damage_of(2);
    shell.splash_radius = 50.0F;
    static_cast<void>(spawn_shot(app, 100.0F, 100.0F, shell));
    app.run_frames(1);
    CHECK(life_of(app, centre) == 3);
    CHECK(life_of(app, near) == 3);
    CHECK(life_of(app, far) == 5);
}

TEST_CASE("combat2d: a piercing projectile goes through several targets, one hit each") {
    auto app = game();
    const Entity first = spawn_target(app, 100.0F, 100.0F, 5);
    const Entity second = spawn_target(app, 200.0F, 100.0F, 5);
    combat2d::Projectile arrow = damage_of(1);
    arrow.pierce = 1;
    const Entity shot = spawn_shot(app, 100.0F, 100.0F, arrow);
    CHECK(app.world().insert(shot, transform2d::Velocity{0.0F, 0.0F}));
    app.run_frames(1);
    CHECK(life_of(app, first) == 4);
    REQUIRE(app.world().is_alive(shot));
    app.run_frames(3); // still on the first one: it does not hit it again
    CHECK(life_of(app, first) == 4);
    app.world().get<transform2d::Position>(shot)->x = 200.0F;
    app.run_frames(1);
    CHECK(life_of(app, second) == 4);
    CHECK(!app.world().is_alive(shot)); // the pierce is used up
}

TEST_CASE("combat2d: a projectile can slow what it hits for a while") {
    auto app = game();
    const Entity target = spawn_target(app, 100.0F, 100.0F, 5);
    combat2d::Projectile ice = damage_of(1);
    ice.on_hit_speed_factor = 0.4F;
    ice.on_hit_speed_ticks = 90;
    static_cast<void>(spawn_shot(app, 100.0F, 100.0F, ice));
    app.run_frames(1);
    REQUIRE(app.world().has<transform2d::SpeedScale>(target));
    CHECK(app.world().get<transform2d::SpeedScale>(target)->factor == 0.4F);
    CHECK(app.world().get<transform2d::SpeedScale>(target)->ticks > 80U);
}

TEST_CASE("combat2d: a homing projectile turns towards its target and keeps its speed") {
    auto app = game();
    static_cast<void>(spawn_target(app, 100.0F, 0.0F, 5));
    combat2d::Projectile missile = damage_of(1);
    missile.homing = 0.5F;
    const Entity shot = spawn_shot(app, 0.0F, 0.0F, missile);
    CHECK(app.world().insert(shot, transform2d::Velocity{0.0F, 100.0F})); // not at it: down
    app.run_frames(1);
    const auto* velocity = app.world().get<transform2d::Velocity>(shot);
    CHECK(velocity->x > 20.0F); // it turned towards the target on its right
    CHECK(std::fabs(std::sqrt(velocity->x * velocity->x + velocity->y * velocity->y) - 100.0F) <
          0.5F);
}

TEST_CASE("combat2d: touching hurts what has health, on the layers asked for") {
    auto app = game();
    const Entity player = spawn_target(app, 100.0F, 100.0F, 3, player_layer);
    const Entity monster_friend = spawn_target(app, 300.0F, 100.0F, 3, monster_layer);
    const Entity biter = spawn_box(app, 105.0F, 100.0F, monster_layer);
    combat2d::ContactDamage bite;
    bite.layers = player_layer;
    CHECK(app.world().insert(biter, bite));
    const Entity biter_two = spawn_box(app, 300.0F, 105.0F, monster_layer);
    CHECK(app.world().insert(biter_two, bite));
    app.run_frames(1);
    CHECK(life_of(app, player) == 2);
    CHECK(life_of(app, monster_friend) == 3); // not a layer it hurts
}

TEST_CASE(
    "combat2d: a monster with three lives hurts a three-lives player on contact, now and then") {
    auto app = game();
    health::Health player_life;
    player_life.amount = 3;
    player_life.max = 3;
    player_life.despawn_on_zero = false;
    player_life.on_zero = 0;
    const Entity player = spawn_box(app, 100.0F, 100.0F, player_layer);
    CHECK(app.world().insert(player, player_life));
    const Entity monster = spawn_target(app, 105.0F, 100.0F, 3);
    combat2d::ContactDamage bite;
    bite.layers = player_layer;
    bite.cooldown = 9;
    CHECK(app.world().insert(monster, bite));
    app.run_frames(1);
    CHECK(life_of(app, player) == 2);
    app.run_frames(9); // the bite recovers during its cooldown
    CHECK(life_of(app, player) == 2);
    app.run_frames(1);
    CHECK(life_of(app, player) == 1);
    app.run_frames(10);
    CHECK(life_of(app, player) == 0);
    CHECK(app.world().has<health::Dead>(player));
    CHECK(app.world().is_alive(player)); // kept, dead
}

TEST_CASE("combat2d: that monster, with three lives too, is killed by three shots") {
    auto app = game();
    const Entity monster = spawn_target(app, 105.0F, 100.0F, 3);
    for (int i = 0; i < 3; ++i) {
        CHECK(app.world().is_alive(monster));
        static_cast<void>(spawn_shot(app, 105.0F, 100.0F, damage_of(1)));
        app.run_frames(1);
    }
    CHECK(!app.world().is_alive(monster));
}

TEST_CASE("combat2d: the dead are not hurt again, and do not hurt") {
    auto app = game();
    const Entity target = spawn_target(app, 100.0F, 100.0F, 1);
    app.world().get<health::Health>(target)->despawn_on_zero = false;
    static_cast<void>(spawn_shot(app, 100.0F, 100.0F, damage_of(1)));
    app.run_frames(1);
    REQUIRE(app.world().has<health::Dead>(target));
    const Entity shot = spawn_shot(app, 100.0F, 100.0F, damage_of(1));
    app.run_frames(2);
    CHECK(app.world().is_alive(shot)); // a dead target is not a target
}
