#include "ge/app/app.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/app/spawn.hpp"
#include "ge/mod/autofire/autofire.gen.hpp"
#include "ge/mod/collision2d/collision2d.gen.hpp"
#include "ge/mod/health/health.gen.hpp"
#include "ge/mod/path2d/path2d.gen.hpp"
#include "ge/mod/sprite2d/sprite2d.gen.hpp"
#include "ge/mod/text2d/text2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"
#include "ge/mod/ui/ui.gen.hpp"

#include "mini_test.hpp"

#include <cmath>
#include <memory>
#include <vector>

namespace {

namespace autofire = ge::mod::autofire;
namespace collision2d = ge::mod::collision2d;
namespace health = ge::mod::health;
namespace path2d = ge::mod::path2d;
namespace transform2d = ge::mod::transform2d;

using ge::app::SpawnRequest;
using ge::ecs::Entity;

// Actions: 0 fired.
struct Scene {
    ge::app::App app;
    std::shared_ptr<std::vector<SpawnRequest>> shots =
        std::make_shared<std::vector<SpawnRequest>>();
    Entity gun;

    explicit Scene(autofire::AutoFire setup = {}) {
        transform2d::install(app);
        collision2d::install(app);
        ge::mod::sprite2d::install(app);
        ge::mod::text2d::install(app);
        ge::mod::ui::install(app);
        health::install(app);
        path2d::install(app);
        autofire::install(app);
        CHECK(app.module_problems().empty());
        CHECK(app.schedule().problems().empty());
        app.world().insert_resource(ge::app::ActionNames{{"fired"}});
        app.set_prefab_spawner([shots = shots](ge::app::App&, const SpawnRequest& request) {
            shots->push_back(request);
        });
        gun = app.world().spawn();
        CHECK(app.world().insert(gun, transform2d::Position{0.0F, 0.0F}));
        setup.range = 100.0F;
        setup.cooldown = 10;
        setup.prefab = 3;
        setup.projectile_speed = 200.0F;
        setup.action = 0;
        CHECK(app.world().insert(gun, setup));
    }

    Entity enemy(float x, float y, std::int32_t amount = 5, float progress = 0.0F,
                 std::uint32_t layer = 2U) {
        const Entity entity = app.world().spawn();
        CHECK(app.world().insert(entity, transform2d::Position{x, y}));
        CHECK(app.world().insert(entity, collision2d::Collider{10.0F, 10.0F, layer, 0U, false}));
        health::Health life;
        life.amount = amount;
        life.max = amount;
        CHECK(app.world().insert(entity, life));
        if (progress > 0.0F) {
            path2d::PathFollower follower;
            follower.progress = progress;
            CHECK(app.world().insert(entity, follower));
        }
        return entity;
    }

    autofire::AutoFire& setup() { return *app.world().get<autofire::AutoFire>(gun); }
};

} // namespace

TEST_CASE("autofire: it fires at a target in range, towards it, with the speed it has") {
    Scene scene;
    scene.enemy(60.0F, 0.0F);
    scene.app.run_frames(1);
    REQUIRE(scene.shots->size() == 1U);
    const SpawnRequest& shot = scene.shots->front();
    CHECK(shot.prefab == 3U);
    CHECK(shot.has_velocity);
    CHECK(std::fabs(shot.vx - 200.0F) < 0.01F);
    CHECK(std::fabs(shot.vy) < 0.01F);
    scene.app.run_frames(1);
    CHECK(scene.app.world().resource<ge::app::Actions>()->pressed("fired"));
}

TEST_CASE("autofire: nothing out of range, on another layer, or dead is a target") {
    Scene scene;
    scene.setup().target_layer = 2U;
    scene.enemy(150.0F, 0.0F);
    scene.enemy(20.0F, 0.0F, 5, 0.0F, 4U);
    const Entity dead = scene.enemy(30.0F, 0.0F);
    CHECK(scene.app.world().insert(dead, health::Dead{}));
    scene.app.run_frames(20);
    CHECK(scene.shots->empty());
}

TEST_CASE("autofire: it fires once per cooldown") {
    Scene scene;
    scene.enemy(50.0F, 0.0F);
    scene.app.run_frames(35); // ticks 0, 11, 22 and 33
    CHECK(scene.shots->size() == 4U);
}

TEST_CASE("autofire: the closest by default; the first and the last by their progress") {
    Scene scene;
    scene.enemy(80.0F, 0.0F, 5, 500.0F); // the furthest along its path, but not the closest
    scene.enemy(30.0F, 0.0F, 5, 100.0F); // the closest
    scene.enemy(0.0F, -60.0F, 5, 10.0F); // the least advanced
    scene.app.run_frames(1);
    REQUIRE(scene.shots->size() == 1U);
    CHECK(scene.shots->back().vx > 190.0F); // closest: straight to the right

    Scene first;
    first.setup().targeting = 1;
    first.enemy(80.0F, 0.0F, 5, 500.0F);
    first.enemy(30.0F, 0.0F, 5, 100.0F);
    first.app.run_frames(1);
    REQUIRE(first.shots->size() == 1U);
    CHECK(std::fabs(first.shots->back().vx - 200.0F) < 0.01F);

    Scene last;
    last.setup().targeting = 2;
    last.enemy(80.0F, 0.0F, 5, 500.0F);
    last.enemy(0.0F, -60.0F, 5, 10.0F);
    last.app.run_frames(1);
    REQUIRE(last.shots->size() == 1U);
    CHECK(last.shots->back().vy < -190.0F); // up: the one that has walked least
}

TEST_CASE("autofire: the strongest and the weakest") {
    Scene strong;
    strong.setup().targeting = 3;
    strong.enemy(30.0F, 0.0F, 2);
    strong.enemy(0.0F, 50.0F, 9);
    strong.app.run_frames(1);
    REQUIRE(strong.shots->size() == 1U);
    CHECK(strong.shots->back().vy > 190.0F); // the one with 9

    Scene weak;
    weak.setup().targeting = 4;
    weak.enemy(30.0F, 0.0F, 2);
    weak.enemy(0.0F, 50.0F, 9);
    weak.app.run_frames(1);
    REQUIRE(weak.shots->size() == 1U);
    CHECK(weak.shots->back().vx > 190.0F);
}

TEST_CASE("autofire: a Disabled shooter does not fire") {
    Scene scene;
    scene.enemy(50.0F, 0.0F);
    CHECK(scene.app.world().insert(scene.gun, transform2d::Disabled{}));
    scene.app.run_frames(5);
    CHECK(scene.shots->empty());
}
