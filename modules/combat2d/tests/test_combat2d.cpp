#include "ge/app/app.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/app/viewport.hpp"
#include "ge/mod/camera2d/camera2d.gen.hpp"
#include "ge/mod/collision2d/collision2d.gen.hpp"
#include "ge/mod/combat2d/combat2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

namespace {

namespace camera2d = ge::mod::camera2d;
namespace collision2d = ge::mod::collision2d;
namespace combat2d = ge::mod::combat2d;
namespace transform2d = ge::mod::transform2d;

using ge::ecs::Entity;

constexpr std::uint32_t player_layer = 1U;
constexpr std::uint32_t monster_layer = 2U;

ge::app::App game() {
    ge::app::App app;
    transform2d::install(app);
    collision2d::install(app);
    camera2d::install(app);
    combat2d::install(app);
    CHECK(app.module_problems().empty());
    CHECK(app.schedule().problems().empty());
    app.world().insert_resource(ge::app::Viewport{480, 720});
    app.world().insert_resource(ge::app::ActionNames{{"died", "other"}});
    return app;
}

Entity spawn_camera(ge::app::App& app, float y) {
    const Entity camera = app.world().spawn();
    CHECK(app.world().insert(camera, transform2d::Position{240.0F, y}));
    CHECK(app.world().insert(camera, camera2d::Camera{}));
    return camera;
}

Entity spawn_box(ge::app::App& app, float x, float y, std::uint32_t layer) {
    const Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{x, y}));
    CHECK(
        app.world().insert(entity, collision2d::Collider{20.0F, 20.0F, layer, 0xFFFFFFFFU, false}));
    CHECK(app.world().insert(entity, collision2d::Contacts{}));
    return entity;
}

Entity spawn_shot(ge::app::App& app, float x, float y, int damage) {
    const Entity entity = spawn_box(app, x, y, 4U);
    CHECK(app.world().insert(entity, combat2d::Projectile{damage}));
    return entity;
}

Entity spawn_target(ge::app::App& app, float x, float y, int health) {
    const Entity entity = spawn_box(app, x, y, monster_layer);
    CHECK(app.world().insert(entity, combat2d::Target{health}));
    return entity;
}

Entity spawn_mortal(ge::app::App& app, float x, float y, combat2d::Mortal mortal) {
    const Entity entity = spawn_box(app, x, y, player_layer);
    CHECK(app.world().insert(entity, mortal));
    return entity;
}

bool dead(ge::app::App& app, Entity entity) {
    return app.world().get<combat2d::Mortal>(entity)->dead;
}

} // namespace

TEST_CASE("combat2d: a projectile destroys a target with one life, and itself") {
    auto app = game();
    const Entity target = spawn_target(app, 100.0F, 100.0F, 1);
    const Entity shot = spawn_shot(app, 105.0F, 95.0F, 1);
    app.run_frames(1);
    CHECK(!app.world().is_alive(target));
    CHECK(!app.world().is_alive(shot));
}

TEST_CASE("combat2d: a target with several lives loses one per hit") {
    auto app = game();
    const Entity target = spawn_target(app, 100.0F, 100.0F, 3);
    const Entity first = spawn_shot(app, 100.0F, 100.0F, 1);
    app.run_frames(1);
    REQUIRE(app.world().is_alive(target));
    CHECK(app.world().get<combat2d::Target>(target)->health == 2);
    CHECK(!app.world().is_alive(first));
    static_cast<void>(spawn_shot(app, 100.0F, 100.0F, 2));
    app.run_frames(1);
    CHECK(!app.world().is_alive(target));
}

TEST_CASE("combat2d: a projectile that misses goes on, and hits only one target") {
    auto app = game();
    const Entity far = spawn_target(app, 300.0F, 300.0F, 1);
    const Entity near_a = spawn_target(app, 100.0F, 100.0F, 1);
    const Entity near_b = spawn_target(app, 105.0F, 100.0F, 1);
    const Entity shot = spawn_shot(app, 102.0F, 100.0F, 1);
    app.run_frames(1);
    CHECK(app.world().is_alive(far));
    CHECK(!app.world().is_alive(shot));
    CHECK(app.world().is_alive(near_a) != app.world().is_alive(near_b)); // exactly one died
}

TEST_CASE("combat2d: a mortal dies on touching a harmful layer, not another one") {
    auto app = game();
    static_cast<void>(spawn_camera(app, 360.0F));
    combat2d::Mortal hurtable;
    hurtable.hurt_layers = monster_layer;
    const Entity safe = spawn_mortal(app, 50.0F, 100.0F, hurtable);
    const Entity unlucky = spawn_mortal(app, 300.0F, 100.0F, hurtable);
    static_cast<void>(spawn_box(app, 305.0F, 100.0F, monster_layer));
    static_cast<void>(spawn_box(app, 50.0F, 105.0F, 8U)); // touches `safe`, but is harmless
    app.run_frames(1);
    CHECK(!dead(app, safe));
    CHECK(dead(app, unlucky));
}

TEST_CASE("combat2d: with fall_death a mortal dies under the bottom edge of the screen") {
    auto app = game();
    const Entity camera = spawn_camera(app, 360.0F); // the screen shows y 0 to 720
    combat2d::Mortal climber;
    climber.fall_death = true;
    climber.fall_margin = 50.0F;
    const Entity inside = spawn_mortal(app, 100.0F, 700.0F, climber);
    const Entity margin = spawn_mortal(app, 200.0F, 760.0F, climber); // 770 is the limit
    const Entity below = spawn_mortal(app, 300.0F, 780.0F, climber);
    const combat2d::Mortal stays;
    const Entity careless = spawn_mortal(app, 400.0F, 5000.0F, stays); // fall_death is off
    app.run_frames(1);
    CHECK(!dead(app, inside));
    CHECK(!dead(app, margin));
    CHECK(dead(app, below));
    CHECK(!dead(app, careless));
    app.world().get<transform2d::Position>(camera)->y =
        100.0F; // the screen climbs: its bottom is at 460
    app.run_frames(1);
    CHECK(dead(app, inside));
}

TEST_CASE("combat2d: a death raises its action once, for the whole next tick") {
    auto app = game();
    static_cast<void>(spawn_camera(app, 360.0F));
    combat2d::Mortal climber;
    climber.fall_death = true;
    climber.on_death = 0; // "died"
    static_cast<void>(spawn_mortal(app, 100.0F, 2000.0F, climber));
    int pressed_ticks = 0;
    app.add_system(ge::app::Stage::FixedUpdate, [&pressed_ticks](ge::ecs::World& world) {
        if (world.resource<ge::app::Actions>()->pressed("died")) {
            ++pressed_ticks;
        }
    });
    app.run_frames(6);
    CHECK(pressed_ticks == 1);
}

TEST_CASE("combat2d: a mortal with no death action still dies quietly") {
    auto app = game();
    static_cast<void>(spawn_camera(app, 360.0F));
    combat2d::Mortal climber;
    climber.fall_death = true; // on_death is "none"
    const Entity entity = spawn_mortal(app, 100.0F, 2000.0F, climber);
    app.run_frames(3);
    CHECK(dead(app, entity));
    CHECK(app.world().get<combat2d::Mortal>(entity)->announced);
    CHECK(!app.world().resource<ge::app::Actions>()->down("died"));
}
