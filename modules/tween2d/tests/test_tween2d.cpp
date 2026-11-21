#include "ge/app/app.hpp"
#include "ge/app/fields.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"
#include "ge/mod/tween2d/tween2d.gen.hpp"

#include "mini_test.hpp"

#include <algorithm>
#include <string>

namespace {

namespace transform2d = ge::mod::transform2d;
namespace tween2d = ge::mod::tween2d;

ge::app::App game() {
    ge::app::App app;
    transform2d::install(app);
    tween2d::install(app);
    CHECK(app.module_problems().empty());
    CHECK(app.schedule().problems().empty());
    app.world().insert_resource(ge::app::ActionNames{{"finished"}});
    return app;
}

std::uint32_t field_index(const ge::app::App& app, const std::string& name) {
    const auto* table = app.world().resource<ge::app::FieldTable>();
    const auto found = std::find_if(table->entries.begin(), table->entries.end(),
                                    [&name](const auto& entry) { return entry.name == name; });
    CHECK(found != table->entries.end());
    return static_cast<std::uint32_t>(found - table->entries.begin());
}

float x_of(ge::app::App& app, ge::ecs::Entity entity) {
    return app.world().get<transform2d::Position>(entity)->x;
}

tween2d::Tween slide(ge::app::App& app, float from, float to, std::uint32_t ticks) {
    tween2d::Tween tween;
    tween.field = field_index(app, "transform2d.Position.x");
    tween.from = from;
    tween.to = to;
    tween.ticks = ticks;
    return tween;
}

} // namespace

TEST_CASE("tween2d: a number goes from one value to another in the given ticks") {
    ge::app::App app = game();
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{0.0F, 0.0F}));
    CHECK(app.world().insert(entity, slide(app, 10.0F, 20.0F, 10)));
    app.run_frames(5);
    CHECK(x_of(app, entity) == 15.0F);
    app.run_frames(5);
    CHECK(x_of(app, entity) == 20.0F);
    app.run_frames(5);
    CHECK(x_of(app, entity) == 20.0F); // it ended; nothing moves it any more
    CHECK(app.world().is_alive(entity));
    CHECK(!app.world().has<tween2d::Tween>(entity)); // the Tween is removed, the entity stays
}

TEST_CASE("tween2d: a delay waits, and from_current starts from where the number is") {
    ge::app::App app = game();
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{100.0F, 0.0F}));
    tween2d::Tween tween = slide(app, 0.0F, 200.0F, 4);
    tween.delay = 3;
    tween.from_current = true;
    CHECK(app.world().insert(entity, tween));
    app.run_frames(3);
    CHECK(x_of(app, entity) == 100.0F); // still waiting
    app.run_frames(2);
    CHECK(x_of(app, entity) == 150.0F); // two of four ticks, from 100 to 200
    app.run_frames(2);
    CHECK(x_of(app, entity) == 200.0F);
}

TEST_CASE("tween2d: every curve starts and ends on the same values") {
    for (std::uint32_t easing = 0; easing <= 4; ++easing) {
        ge::app::App app = game();
        const ge::ecs::Entity entity = app.world().spawn();
        CHECK(app.world().insert(entity, transform2d::Position{0.0F, 0.0F}));
        tween2d::Tween tween = slide(app, 0.0F, 100.0F, 20);
        tween.easing = easing;
        CHECK(app.world().insert(entity, tween));
        app.run_frames(1);
        CHECK(x_of(app, entity) < 30.0F);
        app.run_frames(19);
        CHECK(x_of(app, entity) == 100.0F);
    }
}

namespace {

/// Where a number going from 0 to 100 in 10 ticks is after 5 ticks, with this curve.
float halfway_with(std::uint32_t easing) {
    ge::app::App app = game();
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{0.0F, 0.0F}));
    tween2d::Tween tween = slide(app, 0.0F, 100.0F, 10);
    tween.easing = easing;
    CHECK(app.world().insert(entity, tween));
    app.run_frames(5);
    return x_of(app, entity);
}

} // namespace

TEST_CASE("tween2d: the curves differ: a slow start lags a linear one, a slow end leads it") {
    CHECK(halfway_with(1) < halfway_with(0));
    CHECK(halfway_with(2) > halfway_with(0));
}

TEST_CASE("tween2d: the overshooting curve goes past the goal before settling on it") {
    ge::app::App app = game();
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{0.0F, 0.0F}));
    tween2d::Tween tween = slide(app, 0.0F, 100.0F, 20);
    tween.easing = 4;
    CHECK(app.world().insert(entity, tween));
    float highest = 0.0F;
    for (int i = 0; i < 20; ++i) {
        app.run_frames(1);
        highest = std::max(highest, x_of(app, entity));
    }
    CHECK(highest > 100.0F);
    CHECK(x_of(app, entity) == 100.0F);
}

TEST_CASE("tween2d: a finished tween raises its action and can destroy its entity") {
    ge::app::App app = game();
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{}));
    tween2d::Tween tween = slide(app, 0.0F, 1.0F, 3);
    tween.use_action = true;
    tween.despawn_when_done = true;
    CHECK(app.world().insert(entity, tween));
    app.run_frames(3);
    CHECK(!app.world().is_alive(entity));
    app.run_frames(1);
    CHECK(app.world().resource<ge::app::Actions>()->pressed("finished"));
}

TEST_CASE("tween2d: a tween entity can move the number of its owner") {
    ge::app::App app = game();
    const ge::ecs::Entity owner = app.world().spawn();
    CHECK(app.world().insert(owner, transform2d::Position{0.0F, 0.0F}));
    const ge::ecs::Entity effect = app.world().spawn();
    CHECK(app.world().insert(effect, transform2d::Owner{owner.to_link()}));
    tween2d::Tween tween = slide(app, 0.0F, 8.0F, 4);
    tween.on_owner = true;
    tween.despawn_when_done = true;
    CHECK(app.world().insert(effect, tween));
    app.run_frames(4);
    CHECK(x_of(app, owner) == 8.0F);
    app.run_frames(1);
    CHECK(!app.world().is_alive(effect));
    CHECK(app.world().is_alive(owner));
}

TEST_CASE("tween2d: a tween with no target, or on a dead owner, still ends") {
    ge::app::App app = game();
    const ge::ecs::Entity effect = app.world().spawn();
    tween2d::Tween tween = slide(app, 0.0F, 8.0F, 2);
    tween.on_owner = true;
    tween.despawn_when_done = true;
    CHECK(app.world().insert(effect, tween));
    app.run_frames(3);
    CHECK(!app.world().is_alive(effect));
}

TEST_CASE("tween2d: Settle eases a number towards its goal and then lands on it") {
    ge::app::App app = game();
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{0.0F, 0.0F}));
    tween2d::Settle settle;
    settle.field = field_index(app, "transform2d.Position.x");
    settle.toward = 100.0F;
    settle.rate = 0.5F;
    CHECK(app.world().insert(entity, settle));
    app.run_frames(1);
    CHECK(x_of(app, entity) == 50.0F);
    app.run_frames(1);
    CHECK(x_of(app, entity) == 75.0F);
    app.run_frames(30);
    CHECK(x_of(app, entity) == 100.0F);
    app.world().get<tween2d::Settle>(entity)->toward = 0.0F; // the goal can change
    app.run_frames(40);
    CHECK(x_of(app, entity) == 0.0F);
}

TEST_CASE("tween2d: the Scale of an entity can be animated, which is how a pop is made") {
    ge::app::App app = game();
    const ge::ecs::Entity tile = app.world().spawn();
    CHECK(app.world().insert(tile, transform2d::Scale{1.25F}));
    tween2d::Settle settle;
    settle.field = field_index(app, "transform2d.Scale.factor");
    settle.toward = 1.0F;
    settle.rate = 0.5F;
    CHECK(app.world().insert(tile, settle));
    app.run_frames(1);
    CHECK(app.world().get<transform2d::Scale>(tile)->factor == 1.125F);
    app.run_frames(20);
    CHECK(app.world().get<transform2d::Scale>(tile)->factor == 1.0F);
}
