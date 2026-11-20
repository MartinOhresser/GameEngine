#include "ge/app/app.hpp"
#include "ge/app/fields.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/mod/rules/rules.gen.hpp"
#include "ge/mod/sprite2d/sprite2d.gen.hpp"
#include "ge/mod/text2d/text2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"
#include "ge/mod/ui/ui.gen.hpp"

#include "mini_test.hpp"

#include <algorithm>
#include <string>

namespace {

namespace rules = ge::mod::rules;
namespace transform2d = ge::mod::transform2d;
namespace ui = ge::mod::ui;

struct Scene {
    ge::app::App app;

    Scene() {
        transform2d::install(app);
        ge::mod::sprite2d::install(app);
        ge::mod::text2d::install(app);
        ui::install(app);
        rules::install(app);
        CHECK(app.module_problems().empty());
        CHECK(app.schedule().problems().empty());
        app.world().insert_resource(ge::app::ActionNames{{"reached", "set_x", "add_x", "other"}});
    }

    [[nodiscard]] std::uint32_t field(const std::string& name) const {
        const auto* table = app.world().resource<ge::app::FieldTable>();
        const auto found = std::find_if(table->entries.begin(), table->entries.end(),
                                        [&name](const auto& entry) { return entry.name == name; });
        CHECK(found != table->entries.end());
        return static_cast<std::uint32_t>(found - table->entries.begin());
    }

    [[nodiscard]] bool pressed(const char* action) const {
        return app.world().resource<ge::app::Actions>()->pressed(action);
    }

    [[nodiscard]] float x(ge::ecs::Entity entity) const {
        return app.world().get<transform2d::Position>(entity)->x;
    }
};

} // namespace

TEST_CASE("rules: a Watch raises its action when the number comes into its range") {
    Scene scene;
    const auto entity = scene.app.world().spawn();
    CHECK(scene.app.world().insert(entity, transform2d::Position{0.0F, 0.0F}));
    rules::Watch watch;
    watch.field = scene.field("transform2d.Position.x");
    watch.min = 10.0F;
    watch.action = 0;
    CHECK(scene.app.world().insert(entity, watch));
    scene.app.run_frames(2);
    CHECK(!scene.pressed("reached"));
    scene.app.world().get<transform2d::Position>(entity)->x = 12.0F;
    scene.app.run_frames(2);
    CHECK(scene.pressed("reached"));
}

namespace {

/// How many ticks raise the action while the number of the entity takes these values in turn.
int raised_while(Scene& scene, ge::ecs::Entity entity) {
    int raised = 0;
    for (const float value : {5.0F, 0.0F, 3.0F, -1.0F}) {
        scene.app.world().get<transform2d::Position>(entity)->x = value;
        for (int i = 0; i < 3; ++i) {
            scene.app.run_frames(1);
            raised += scene.pressed("reached") ? 1 : 0;
        }
    }
    return raised;
}

int raised_by_falling(bool once) {
    Scene scene;
    const auto entity = scene.app.world().spawn();
    CHECK(scene.app.world().insert(entity, transform2d::Position{0.0F, 0.0F}));
    rules::Watch watch;
    watch.field = scene.field("transform2d.Position.x");
    watch.max = 0.0F; // "falls to 0 or less"
    watch.once = once;
    CHECK(scene.app.world().insert(entity, watch));
    return raised_while(scene, entity);
}

} // namespace

TEST_CASE("rules: a Watch raises again after the number left the range, unless it is once") {
    CHECK(raised_by_falling(false) == 2);
    CHECK(raised_by_falling(true) == 1);
}

TEST_CASE("rules: a Watch can look at the entity that has a tag") {
    Scene scene;
    const auto base = scene.app.world().spawn();
    CHECK(scene.app.world().insert(base, transform2d::Position{50.0F, 0.0F}));
    CHECK(scene.app.world().insert(base, ui::Tag{1}));
    const auto rule = scene.app.world().spawn();
    rules::Watch watch;
    watch.own = false;
    watch.source = 1;
    watch.field = scene.field("transform2d.Position.x");
    watch.min = 100.0F;
    CHECK(scene.app.world().insert(rule, watch));
    scene.app.run_frames(2);
    CHECK(!scene.pressed("reached"));
    scene.app.world().get<transform2d::Position>(base)->x = 100.0F; // both ends are inside
    scene.app.run_frames(2);
    CHECK(scene.pressed("reached"));
}

TEST_CASE("rules: OnAction sets or adds to a number when its action is pressed") {
    Scene scene;
    const auto target = scene.app.world().spawn();
    CHECK(scene.app.world().insert(target, transform2d::Position{1.0F, 0.0F}));
    CHECK(scene.app.world().insert(target, ui::Tag{2}));
    const auto setter = scene.app.world().spawn();
    rules::OnAction set;
    set.action = 1;
    set.source = 2;
    set.field = scene.field("transform2d.Position.x");
    set.value = 40.0F;
    CHECK(scene.app.world().insert(setter, set));
    const auto adder = scene.app.world().spawn();
    rules::OnAction add = set;
    add.action = 2;
    add.add = true;
    add.value = 2.0F;
    CHECK(scene.app.world().insert(adder, add));

    scene.app.run_frames(3);
    CHECK(scene.x(target) == 1.0F); // nothing pressed, nothing changed
    scene.app.world().resource<ge::app::Actions>()->raise("set_x");
    scene.app.run_frames(2);
    CHECK(scene.x(target) == 40.0F);
    scene.app.world().resource<ge::app::Actions>()->raise("add_x");
    scene.app.run_frames(2);
    CHECK(scene.x(target) == 42.0F);
}

TEST_CASE("rules: OnAction on its own entity, a missing target and a switched off rule") {
    Scene scene;
    const auto entity = scene.app.world().spawn();
    CHECK(scene.app.world().insert(entity, transform2d::Position{0.0F, 0.0F}));
    rules::OnAction rule;
    rule.action = 3;
    rule.own = true;
    rule.field = scene.field("transform2d.Position.x");
    rule.value = 9.0F;
    CHECK(scene.app.world().insert(entity, rule));
    const auto nobody = scene.app.world().spawn(); // a tag nothing has
    rules::OnAction lost = rule;
    lost.own = false;
    lost.source = 7;
    CHECK(scene.app.world().insert(nobody, lost));
    CHECK(scene.app.world().insert(entity, transform2d::Disabled{}));
    scene.app.world().resource<ge::app::Actions>()->raise("other");
    scene.app.run_frames(2);
    CHECK(scene.x(entity) == 0.0F); // switched off
    CHECK(scene.app.world().remove<transform2d::Disabled>(entity));
    scene.app.world().resource<ge::app::Actions>()->raise("other");
    scene.app.run_frames(2);
    CHECK(scene.x(entity) == 9.0F);
}

TEST_CASE("rules: OnAction with a link changes the entity that the source points to") {
    Scene scene;
    const auto target = scene.app.world().spawn();
    CHECK(scene.app.world().insert(target, transform2d::Position{0.0F, 0.0F}));
    const auto bystander = scene.app.world().spawn();
    CHECK(scene.app.world().insert(bystander, transform2d::Position{0.0F, 0.0F}));
    const auto holder = scene.app.world().spawn();
    CHECK(scene.app.world().insert(holder, transform2d::Position{0.0F, 0.0F}));
    CHECK(scene.app.world().insert(holder, transform2d::Owner{target.to_link()}));
    rules::OnAction rule;
    rule.action = 1;
    rule.own = true;
    rule.link = scene.field("transform2d.Owner.entity");
    rule.field = scene.field("transform2d.Position.x");
    rule.value = 5.0F;
    CHECK(scene.app.world().insert(holder, rule));
    scene.app.world().resource<ge::app::Actions>()->raise("set_x");
    scene.app.run_frames(2);
    CHECK(scene.x(target) == 5.0F);
    CHECK(scene.x(holder) == 0.0F);
    CHECK(scene.x(bystander) == 0.0F);
}

TEST_CASE("rules: OnAction with a link to nobody, or to an entity that is gone, does nothing") {
    Scene scene;
    const auto target = scene.app.world().spawn();
    CHECK(scene.app.world().insert(target, transform2d::Position{0.0F, 0.0F}));
    const auto holder = scene.app.world().spawn();
    CHECK(scene.app.world().insert(holder, transform2d::Position{0.0F, 0.0F}));
    CHECK(scene.app.world().insert(holder, transform2d::Owner{0}));
    rules::OnAction rule;
    rule.action = 1;
    rule.own = true;
    rule.link = scene.field("transform2d.Owner.entity");
    rule.field = scene.field("transform2d.Position.x");
    rule.value = 5.0F;
    CHECK(scene.app.world().insert(holder, rule));
    scene.app.world().resource<ge::app::Actions>()->raise("set_x");
    scene.app.run_frames(2);
    CHECK(scene.x(holder) == 0.0F);
    scene.app.world().get<transform2d::Owner>(holder)->entity = target.to_link();
    CHECK(scene.app.world().despawn(target));
    scene.app.world().resource<ge::app::Actions>()->raise("set_x");
    scene.app.run_frames(2);
    CHECK(scene.x(holder) == 0.0F);
}
