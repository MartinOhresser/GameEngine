#include "ge/app/app.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/mod/health/health.gen.hpp"
#include "ge/mod/path2d/path2d.gen.hpp"
#include "ge/mod/sprite2d/sprite2d.gen.hpp"
#include "ge/mod/tag/tag.gen.hpp"
#include "ge/mod/text2d/text2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"
#include "ge/mod/ui/ui.gen.hpp"

#include "mini_test.hpp"

#include <cmath>

namespace {

namespace health = ge::mod::health;
namespace path2d = ge::mod::path2d;
namespace tag = ge::mod::tag;
namespace transform2d = ge::mod::transform2d;

using ge::ecs::Entity;

// Actions: 0 arrived.
struct Scene {
    ge::app::App app;

    Scene() {
        transform2d::install(app);
        ge::mod::sprite2d::install(app);
        ge::mod::text2d::install(app);
        tag::install(app);
        ge::mod::ui::install(app);
        health::install(app);
        path2d::install(app);
        CHECK(app.module_problems().empty());
        CHECK(app.schedule().problems().empty());
        app.world().insert_resource(ge::app::ActionNames{{"arrived"}});
    }

    Entity point(std::uint32_t path, std::uint32_t index, float x, float y) {
        const Entity entity = app.world().spawn();
        CHECK(app.world().insert(entity, transform2d::Position{x, y}));
        CHECK(app.world().insert(entity, path2d::Waypoint{path, index}));
        return entity;
    }

    Entity walker(float x, float y, float speed, std::uint32_t path = 0) {
        const Entity entity = app.world().spawn();
        CHECK(app.world().insert(entity, transform2d::Position{x, y}));
        path2d::PathFollower follower;
        follower.path = path;
        follower.speed = speed;
        follower.reached_action = 0;
        CHECK(app.world().insert(entity, follower));
        return entity;
    }

    [[nodiscard]] transform2d::Position where(Entity entity) const {
        return *app.world().get<transform2d::Position>(entity);
    }
};

} // namespace

TEST_CASE("path2d: a follower walks the waypoints in the order of their index") {
    Scene scene;
    // Made out of order: the index says the order, not the entities.
    scene.point(0, 2, 100.0F, 100.0F);
    scene.point(0, 0, 0.0F, 0.0F);
    scene.point(0, 1, 100.0F, 0.0F);
    const Entity walker = scene.walker(0.0F, 0.0F, 60.0F);
    scene.app.run_frames(60); // one second: 60 units
    CHECK(scene.where(walker).x == 60.0F);
    CHECK(scene.where(walker).y == 0.0F);
    scene.app.run_frames(60); // 40 more along the first leg, then 20 down
    CHECK(scene.where(walker).x == 100.0F);
    CHECK(std::fabs(scene.where(walker).y - 20.0F) < 0.5F);
    CHECK(std::fabs(scene.app.world().get<path2d::PathFollower>(walker)->progress - 120.0F) < 0.5F);
}

TEST_CASE("path2d: a follower that reaches the end hurts its target, says so and disappears") {
    Scene scene;
    scene.point(0, 0, 0.0F, 0.0F);
    scene.point(0, 1, 30.0F, 0.0F);
    const Entity base = scene.app.world().spawn();
    health::Health life;
    life.amount = 10;
    life.max = 10;
    life.despawn_on_zero = false;
    CHECK(scene.app.world().insert(base, life));
    CHECK(scene.app.world().insert(base, tag::Tag{4}));
    const Entity walker = scene.walker(0.0F, 0.0F, 60.0F);
    auto* follower = scene.app.world().get<path2d::PathFollower>(walker);
    follower->end_target = 4;
    follower->end_damage = 3;
    for (int tick = 0; tick < 60 && scene.app.world().is_alive(walker); ++tick) {
        scene.app.run_frames(1);
    }
    CHECK(!scene.app.world().is_alive(walker));
    CHECK(scene.app.world().get<health::Health>(base)->amount == 7);
    scene.app.run_frames(1); // the action raised on arrival is pressed in the next tick
    CHECK(scene.app.world().resource<ge::app::Actions>()->pressed("arrived"));
}

TEST_CASE("path2d: a SpeedScale slows a follower") {
    Scene scene;
    scene.point(0, 0, 0.0F, 0.0F);
    scene.point(0, 1, 500.0F, 0.0F);
    const Entity slow = scene.walker(0.0F, 0.0F, 60.0F);
    CHECK(scene.app.world().insert(slow, transform2d::SpeedScale{0.5F, 0}));
    const Entity usual = scene.walker(0.0F, 0.0F, 60.0F);
    scene.app.run_frames(60);
    CHECK(std::fabs(scene.where(slow).x - 30.0F) < 0.5F);
    CHECK(std::fabs(scene.where(usual).x - 60.0F) < 0.5F);
}

TEST_CASE("path2d: paths do not mix, and a follower without a path stays") {
    Scene scene;
    scene.point(0, 0, 0.0F, 0.0F);
    scene.point(0, 1, 1000.0F, 0.0F);
    scene.point(1, 0, 0.0F, 50.0F);
    scene.point(1, 1, 0.0F, 1000.0F);
    const Entity along_x = scene.walker(0.0F, 0.0F, 60.0F, 0);
    const Entity along_y = scene.walker(0.0F, 50.0F, 60.0F, 1);
    const Entity lost = scene.walker(5.0F, 5.0F, 60.0F, 7);
    scene.app.run_frames(60);
    CHECK(scene.where(along_x).y == 0.0F);
    CHECK(std::fabs(scene.where(along_y).y - 110.0F) < 0.5F);
    CHECK(scene.where(lost).x == 5.0F);
}

TEST_CASE("path2d: a Disabled follower waits") {
    Scene scene;
    scene.point(0, 0, 0.0F, 0.0F);
    scene.point(0, 1, 100.0F, 0.0F);
    const Entity walker = scene.walker(0.0F, 0.0F, 60.0F);
    CHECK(scene.app.world().insert(walker, transform2d::Disabled{}));
    scene.app.run_frames(30);
    CHECK(scene.where(walker).x == 0.0F);
    CHECK(scene.app.world().remove<transform2d::Disabled>(walker));
    scene.app.run_frames(30);
    CHECK(scene.where(walker).x > 29.0F);
}
