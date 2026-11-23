#include "ge/app/app.hpp"
#include "ge/app/spawn.hpp"
#include "ge/app/viewport.hpp"
#include "ge/mod/transform2d/drawn.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

#include <chrono>

namespace {

namespace transform2d = ge::mod::transform2d;

ge::app::App game() {
    ge::app::App app;
    transform2d::install(app);
    CHECK(app.module_problems().empty());
    CHECK(app.schedule().problems().empty());
    return app;
}

ge::ecs::Entity owned(ge::app::App& app, ge::ecs::Entity owner, float dx, float dy) {
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{}));
    CHECK(app.world().insert(entity, transform2d::Owner{owner.to_link()}));
    CHECK(app.world().insert(entity, transform2d::Attach{dx, dy, true}));
    return entity;
}

ge::ecs::Entity runner(ge::app::App& app, float speed) {
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{0.0F, 0.0F}));
    CHECK(app.world().insert(entity, transform2d::Velocity{speed, 0.0F}));
    return entity;
}

} // namespace

TEST_CASE("transform2d: an entity moves by its velocity") {
    ge::app::App app = game();
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{0.0F, 0.0F}));
    CHECK(app.world().insert(entity, transform2d::Velocity{60.0F, -30.0F}));
    app.run_frames(60); // one second at the default 60 ticks per second
    CHECK(app.world().get<transform2d::Position>(entity)->x > 59.0F);
    CHECK(app.world().get<transform2d::Position>(entity)->y < -29.0F);
}

TEST_CASE("transform2d: an entity links to another and back, and a stale link finds nothing") {
    ge::app::App app = game();
    const ge::ecs::Entity first = app.world().spawn();
    CHECK(ge::ecs::Entity::from_link(first.to_link()) == first);
    CHECK(ge::ecs::Entity::from_link(0U).is_null());
    CHECK(ge::ecs::Entity{}.to_link() == 0U);
    CHECK(app.world().despawn(first));
    const ge::ecs::Entity reused = app.world().spawn(); // same slot, next generation
    CHECK(reused.index() == first.index());
    CHECK(!app.world().is_alive(ge::ecs::Entity::from_link(first.to_link())));
}

TEST_CASE("transform2d: an attached entity follows its owner at an offset") {
    ge::app::App app = game();
    const ge::ecs::Entity owner = app.world().spawn();
    CHECK(app.world().insert(owner, transform2d::Position{100.0F, 50.0F}));
    CHECK(app.world().insert(owner, transform2d::Velocity{60.0F, 0.0F}));
    const ge::ecs::Entity bar = owned(app, owner, 0.0F, -20.0F);
    app.run_frames(60);
    const auto* at = app.world().get<transform2d::Position>(bar);
    const auto* of = app.world().get<transform2d::Position>(owner);
    REQUIRE(at != nullptr);
    CHECK(at->x == of->x);
    CHECK(at->y == of->y - 20.0F);
}

TEST_CASE("transform2d: an attached entity disappears with its owner, unless told to stay") {
    ge::app::App app = game();
    const ge::ecs::Entity owner = app.world().spawn();
    CHECK(app.world().insert(owner, transform2d::Position{}));
    const ge::ecs::Entity goes = owned(app, owner, 0.0F, 0.0F);
    const ge::ecs::Entity stays = owned(app, owner, 0.0F, 0.0F);
    app.world().get<transform2d::Attach>(stays)->despawn_with_owner = false;
    app.run_frames(1);
    CHECK(app.world().is_alive(goes));
    CHECK(app.world().despawn(owner));
    app.run_frames(2);
    CHECK(!app.world().is_alive(goes));
    CHECK(app.world().is_alive(stays));
}

TEST_CASE("transform2d: an Attach with no Owner stays where it is") {
    ge::app::App app = game();
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{5.0F, 6.0F}));
    CHECK(app.world().insert(entity, transform2d::Attach{1.0F, 1.0F, true}));
    CHECK(app.world().insert(entity, transform2d::Owner{0U}));
    app.run_frames(3);
    CHECK(app.world().is_alive(entity));
    CHECK(app.world().get<transform2d::Position>(entity)->x == 5.0F);
}

TEST_CASE("transform2d: a spawn request carries its owner") {
    ge::app::SpawnRequests requests;
    requests.spawn_owned(3U, 1.0F, 2.0F, 77U);
    requests.spawn(4U, 0.0F, 0.0F);
    REQUIRE(requests.queue.size() == 2U);
    CHECK(requests.queue[0].owner == 77U);
    CHECK(requests.queue[1].owner == 0U);
}

TEST_CASE("transform2d: a SpeedScale changes the speed of one entity, and not of the others") {
    ge::app::App app = game();
    const ge::ecs::Entity slow = runner(app, 60.0F);
    CHECK(app.world().insert(slow, transform2d::SpeedScale{0.5F, 0}));
    const ge::ecs::Entity usual = runner(app, 60.0F);
    app.run_frames(30); // half a second at half speed: 15 units, the other one went 30
    CHECK(app.world().get<transform2d::Position>(slow)->x > 14.5F);
    CHECK(app.world().get<transform2d::Position>(slow)->x < 15.5F);
    CHECK(app.world().get<transform2d::Position>(usual)->x > 29.5F);
}

TEST_CASE("transform2d: a SpeedScale with ticks wears off, and the speed comes back") {
    ge::app::App app = game();
    const ge::ecs::Entity slow = runner(app, 60.0F);
    CHECK(app.world().insert(slow, transform2d::SpeedScale{0.5F, 30}));
    app.run_frames(30);
    CHECK(app.world().get<transform2d::Position>(slow)->x < 15.5F);
    CHECK(!app.world().has<transform2d::SpeedScale>(slow)); // its 30 ticks are over
    app.run_frames(30);
    CHECK(app.world().get<transform2d::Position>(slow)->x > 44.0F);
}

TEST_CASE("transform2d: a SpeedScale without ticks lasts") {
    ge::app::App app = game();
    const ge::ecs::Entity entity = runner(app, 60.0F);
    CHECK(app.world().insert(entity, transform2d::SpeedScale{2.0F, 0}));
    app.run_frames(60);
    CHECK(app.world().get<transform2d::Position>(entity)->x > 119.0F);
    CHECK(app.world().has<transform2d::SpeedScale>(entity));
}

namespace {

ge::ecs::Entity anchored(ge::app::App& app, const transform2d::Anchor& anchor) {
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{}));
    CHECK(app.world().insert(entity, anchor));
    return entity;
}

transform2d::Position position_of(ge::app::App& app, ge::ecs::Entity entity) {
    const auto* position = app.world().get<transform2d::Position>(entity);
    CHECK(position != nullptr);
    return position != nullptr ? *position : transform2d::Position{};
}

} // namespace

TEST_CASE("transform2d: an anchored entity follows the window's edges") {
    auto app = game();
    transform2d::Anchor corner;
    corner.anchor_x = 1.0F;
    corner.offset_x = -10.0F;
    corner.offset_y = 10.0F;
    transform2d::Anchor middle;
    middle.anchor_x = 0.5F;
    middle.anchor_y = 0.5F;
    const auto top_right = anchored(app, corner);
    const auto centre = anchored(app, middle);
    app.run_frames(1);
    CHECK(position_of(app, top_right).x == 790.0F); // 800 by 600 unless the game says otherwise
    CHECK(position_of(app, top_right).y == 10.0F);
    CHECK(position_of(app, centre).x == 400.0F);
    CHECK(position_of(app, centre).y == 300.0F);

    app.world().resource<ge::app::Viewport>()->width = 320U;
    app.world().resource<ge::app::Viewport>()->height = 180U;
    app.run_frames(1);
    CHECK(position_of(app, top_right).x == 310.0F);
    CHECK(position_of(app, centre).y == 90.0F);
}

namespace {

/// One tick of a 60 ticks per second game, as `App` counts it.
constexpr std::chrono::nanoseconds tick_time{1'000'000'000 / 60};

float drawn_x(ge::app::App& app, ge::ecs::Entity entity) {
    const auto* position = app.world().get<transform2d::Position>(entity);
    CHECK(position != nullptr);
    return position == nullptr ? -1.0F
                               : transform2d::drawn_position(app.world(), entity, *position).x;
}

} // namespace

TEST_CASE("transform2d: in real time, an entity is drawn between its last two ticks") {
    ge::app::App app = game();
    app.world().resource<ge::app::Time>()->interpolate = true;
    const ge::ecs::Entity entity = runner(app, 60.0F); // one unit per tick
    app.advance(tick_time);                            // tick 1: at x = 1
    CHECK(app.world().get<transform2d::Position>(entity)->x == 1.0F);
    CHECK(drawn_x(app, entity) == 0.0F); // just reached tick 1: shown where tick 0 was
    app.advance(tick_time / 2);          // half way to tick 2, no tick ran
    CHECK(app.world().get<transform2d::Position>(entity)->x == 1.0F);
    CHECK(drawn_x(app, entity) == 0.5F);
    app.advance(tick_time / 2); // tick 2: at x = 2
    CHECK(drawn_x(app, entity) == 1.0F);
    app.advance(tick_time * 3); // several ticks in one frame: drawn where it is
    CHECK(drawn_x(app, entity) == app.world().get<transform2d::Position>(entity)->x);
}

TEST_CASE("transform2d: without real time, a jump or a new entity, the Position is drawn as is") {
    ge::app::App plain = game();
    const ge::ecs::Entity still = runner(plain, 60.0F);
    plain.advance(tick_time / 2);
    plain.advance(tick_time);
    CHECK(drawn_x(plain, still) == plain.world().get<transform2d::Position>(still)->x);

    ge::app::App app = game();
    app.world().resource<ge::app::Time>()->interpolate = true;
    const ge::ecs::Entity jumper = runner(app, 60.0F * 300.0F); // 300 units a tick: a jump
    app.advance(tick_time);
    app.advance(tick_time / 2);
    CHECK(drawn_x(app, jumper) == app.world().get<transform2d::Position>(jumper)->x);
    CHECK(drawn_x(app, jumper) > 299.0F);            // beyond snap_distance (256): no slide
    const ge::ecs::Entity late = runner(app, 60.0F); // born after the positions were taken
    CHECK(drawn_x(app, late) == 0.0F);
    CHECK(app.world().resource<transform2d::Trail>() != nullptr);
}
