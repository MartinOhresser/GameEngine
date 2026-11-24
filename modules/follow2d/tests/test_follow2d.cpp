#include "ge/app/app.hpp"
#include "ge/mod/follow2d/follow2d.gen.hpp"
#include "ge/mod/tag/tag.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

#include <cmath>

namespace {

namespace follow2d = ge::mod::follow2d;
namespace tag = ge::mod::tag;
namespace transform2d = ge::mod::transform2d;

constexpr std::uint32_t leader_tag = 3U;

ge::app::App game() {
    ge::app::App app;
    transform2d::install(app);
    tag::install(app);
    follow2d::install(app);
    CHECK(app.module_problems().empty());
    CHECK(app.schedule().problems().empty());
    return app;
}

ge::ecs::Entity leader(ge::app::App& app, float x, float y, float speed_x) {
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{x, y}));
    CHECK(app.world().insert(entity, transform2d::Velocity{speed_x, 0.0F}));
    CHECK(app.world().insert(entity, tag::Tag{leader_tag}));
    return entity;
}

ge::ecs::Entity follower(ge::app::App& app, const follow2d::Follow& follow) {
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{}));
    CHECK(app.world().insert(entity, follow));
    return entity;
}

const transform2d::Position& at(ge::app::App& app, ge::ecs::Entity entity) {
    return *app.world().get<transform2d::Position>(entity);
}

} // namespace

TEST_CASE("follow2d: a follower glides to the place near its target, a share at a time") {
    ge::app::App app = game();
    leader(app, 100.0F, 50.0F, 0.0F);
    follow2d::Follow follow;
    follow.target = leader_tag;
    follow.offset_x = -20.0F;
    follow.offset_y = -10.0F;
    follow.stiffness = 0.5F;
    const ge::ecs::Entity entity = follower(app, follow);
    app.run_frames(1);
    CHECK(at(app, entity).x == 40.0F);
    CHECK(at(app, entity).y == 20.0F);
    app.run_frames(40);
    CHECK(std::abs(at(app, entity).x - 80.0F) < 0.01F);
    CHECK(std::abs(at(app, entity).y - 40.0F) < 0.01F);
}

TEST_CASE("follow2d: turn keeps the offset on the same side of where the target goes") {
    ge::app::App app = game();
    const ge::ecs::Entity target = leader(app, 0.0F, 0.0F, -60.0F);
    follow2d::Follow follow;
    follow.target = leader_tag;
    follow.offset_x = -30.0F;
    follow.stiffness = 1.0F;
    follow.turn = true;
    const ge::ecs::Entity entity = follower(app, follow);
    app.run_frames(1);
    // Going left: the follower is on its right.
    CHECK(at(app, entity).x == at(app, target).x + 30.0F);
    app.world().get<transform2d::Velocity>(target)->x = 0.0F;
    app.run_frames(1);
    CHECK(at(app, entity).x == at(app, target).x + 30.0F); // standing still keeps the side
    app.world().get<transform2d::Velocity>(target)->x = 60.0F;
    app.run_frames(1);
    CHECK(at(app, entity).x == at(app, target).x - 30.0F);
    CHECK(app.world().get<follow2d::Follow>(entity)->side == 1);
}

TEST_CASE("follow2d: the wave floats the follower around its place, and comes back") {
    ge::app::App app = game();
    leader(app, 0.0F, 0.0F, 0.0F);
    follow2d::Follow follow;
    follow.target = leader_tag;
    follow.stiffness = 1.0F;
    follow.wave_x = 10.0F;
    follow.wave_y = 4.0F;
    follow.wave_period = 1.0F;
    const ge::ecs::Entity entity = follower(app, follow);
    app.run_frames(15); // a quarter of a second at 60 ticks a second: the x wave at its top
    CHECK(std::abs(at(app, entity).x - 10.0F) < 0.01F);
    CHECK(std::abs(at(app, entity).y) < 0.01F);
    app.run_frames(45);
    CHECK(std::abs(at(app, entity).x) < 0.01F);
    CHECK(std::abs(at(app, entity).y) < 0.01F);
}

TEST_CASE("follow2d: the owner can be the target, and a missing target leaves it alone") {
    ge::app::App app = game();
    const ge::ecs::Entity owner = leader(app, 70.0F, 0.0F, 0.0F);
    follow2d::Follow follow;
    follow.owner = true;
    follow.stiffness = 1.0F;
    const ge::ecs::Entity pet = follower(app, follow);
    CHECK(app.world().insert(pet, transform2d::Owner{owner.to_link()}));
    follow.owner = false;
    follow.target = leader_tag + 1U;
    const ge::ecs::Entity lost = follower(app, follow);
    app.run_frames(1);
    CHECK(at(app, pet).x == 70.0F);
    CHECK(at(app, lost).x == 0.0F);
    static_cast<void>(app.world().despawn(owner));
    app.run_frames(1);
    CHECK(at(app, pet).x == 70.0F);
}
