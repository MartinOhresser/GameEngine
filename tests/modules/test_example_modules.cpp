// The example modules, installed into an app the way a generated game would.

#include "ge/app/app.hpp"
#include "ge/app/input.hpp"
#include "ge/mod/action_control/action_control.gen.hpp"
#include "ge/mod/lifetime/lifetime.gen.hpp"
#include "ge/mod/screen_wrap/screen_wrap.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "../support/mini_test.hpp"

#include <vector>

using ge::app::App;
using ge::ecs::Entity;
namespace lifetime = ge::mod::lifetime;
namespace screen_wrap = ge::mod::screen_wrap;
namespace transform2d = ge::mod::transform2d;

TEST_CASE("modules: installing records the module and its systems") {
    App app;
    transform2d::install(app);
    CHECK(app.has_module("transform2d"));
    CHECK(!app.has_module("lifetime"));
    CHECK(app.module_names() == std::vector<std::string>{"transform2d"});
    CHECK(app.module_problems().empty());
    CHECK(app.schedule().system_names(ge::app::Stage::FixedUpdate) ==
          (std::vector<std::string>{"transform2d.integrate", "transform2d.attach"}));
}

TEST_CASE("modules: the generated info matches module.json") {
    const ge::app::ModuleInfo& info = transform2d::info();
    CHECK(info.name == "transform2d");
    CHECK(info.version == "0.1.0");
    CHECK(info.depends.empty());
    REQUIRE(info.components.size() == 7U);
    CHECK(info.components[0].name == "Position");
    CHECK(info.components[0].replicable);
    REQUIRE(info.components[0].fields.size() == 2U);
    CHECK(info.components[0].fields[1].name == "y");
    CHECK(info.components[0].fields[1].type == "f32");
    REQUIRE(info.systems.size() == 2U);
    CHECK(info.systems[0].name == "transform2d.integrate");
    CHECK(info.systems[0].stage == ge::app::Stage::FixedUpdate);
    REQUIRE(info.systems[0].writes.size() == 1U);
    CHECK(info.systems[0].writes[0] == "transform2d.Position");

    const ge::app::ModuleInfo& wrap = screen_wrap::info();
    REQUIRE(wrap.depends.size() == 1U);
    CHECK(wrap.depends[0] == "transform2d");
    CHECK(!wrap.components[0].replicable);
}

TEST_CASE("modules: a dependency must be installed first") {
    App wrong_order;
    screen_wrap::install(wrong_order);
    REQUIRE(wrong_order.module_problems().size() == 1U);
    CHECK(wrong_order.module_problems()[0].find("needs module 'transform2d'") != std::string::npos);

    App twice;
    transform2d::install(twice);
    transform2d::install(twice);
    REQUIRE(twice.module_problems().size() == 1U);
    CHECK(twice.module_problems()[0].find("installed twice") != std::string::npos);

    App fine;
    transform2d::install(fine);
    screen_wrap::install(fine);
    CHECK(fine.module_problems().empty());
}

TEST_CASE("modules: velocity moves an entity by one tick's worth") {
    App app;
    transform2d::install(app);
    const Entity entity =
        app.world().spawn_with(transform2d::Position{}, transform2d::Velocity{60.0F, -30.0F});
    app.run_frames(60);
    const auto* position = app.world().get<transform2d::Position>(entity);
    REQUIRE(position != nullptr);
    CHECK(position->x > 59.9F);
    CHECK(position->x < 60.1F);
    CHECK(position->y < -29.9F);
    CHECK(position->y > -30.1F);
}

TEST_CASE("modules: lifetime destroys entities when it runs out") {
    App app;
    lifetime::install(app);
    const Entity short_lived = app.world().spawn_with(lifetime::Lifetime{3});
    const Entity long_lived = app.world().spawn_with(lifetime::Lifetime{10});
    app.run_frames(2);
    CHECK(app.world().is_alive(short_lived));
    app.run_frames(1);
    CHECK(!app.world().is_alive(short_lived));
    CHECK(app.world().is_alive(long_lived));
    CHECK(lifetime::Lifetime{}.ticks == 60);
}

TEST_CASE("modules: screen_wrap brings entities back on the other side, after the move") {
    App app;
    transform2d::install(app);
    screen_wrap::install(app);
    CHECK(app.module_problems().empty());
    CHECK(app.schedule().problems().empty());
    CHECK(app.schedule().system_names(ge::app::Stage::FixedUpdate) ==
          (std::vector<std::string>{"transform2d.integrate", "transform2d.attach",
                                    "screen_wrap.wrap"}));

    const Entity right = app.world().spawn_with(transform2d::Position{99.0F, 5.0F},
                                                transform2d::Velocity{120.0F, 0.0F},
                                                screen_wrap::WrapArea{100.0F, 50.0F});
    const Entity up = app.world().spawn_with(transform2d::Position{5.0F, 1.0F},
                                             transform2d::Velocity{0.0F, -120.0F},
                                             screen_wrap::WrapArea{100.0F, 50.0F});
    const Entity unbounded = app.world().spawn_with(transform2d::Position{1.0F, 1.0F},
                                                    transform2d::Velocity{6000.0F, 0.0F});
    app.update();
    CHECK(app.world().get<transform2d::Position>(right)->x < 2.0F);
    CHECK(app.world().get<transform2d::Position>(right)->x >= 0.0F);
    CHECK(app.world().get<transform2d::Position>(up)->y > 47.0F);
    CHECK(app.world().get<transform2d::Position>(up)->y < 50.0F);
    CHECK(app.world().get<transform2d::Position>(unbounded)->x > 100.0F);
}

TEST_CASE("modules: a wrap area of zero leaves the position alone") {
    App app;
    transform2d::install(app);
    screen_wrap::install(app);
    const Entity entity = app.world().spawn_with(transform2d::Position{-3.0F, 4.0F},
                                                 screen_wrap::WrapArea{0.0F, 0.0F});
    app.update();
    CHECK(app.world().get<transform2d::Position>(entity)->x == -3.0F);
    CHECK(app.world().get<transform2d::Position>(entity)->y == 4.0F);
}

TEST_CASE("modules: generated components compare and hash") {
    const transform2d::Position first{1.0F, 2.0F};
    const transform2d::Position same{1.0F, 2.0F};
    const transform2d::Position swapped{2.0F, 1.0F};
    CHECK(first == same);
    CHECK(!(first == swapped));
    CHECK(ge::hash_of(first) == ge::hash_of(same));
    CHECK(ge::hash_of(first) != ge::hash_of(swapped));
}

TEST_CASE("modules: the game can build a component from field values") {
    App app;
    transform2d::install(app);
    const auto& position = transform2d::info().components[0];
    REQUIRE(position.insert != nullptr);
    const Entity entity = app.world().spawn();
    const std::vector<ge::app::FieldValue> values = {{"x", 4.5}, {"y", std::int64_t{7}}};
    position.insert(app.world(), entity, values);
    const auto* made = app.world().get<transform2d::Position>(entity);
    REQUIRE(made != nullptr);
    CHECK(made->x == 4.5F);
    CHECK(made->y == 7.0F);
    // Fields that are not given keep their default.
    const Entity other = app.world().spawn();
    position.insert(app.world(), other, {});
    CHECK(app.world().get<transform2d::Position>(other)->x == 0.0F);
}

TEST_CASE("modules: the game can read a field back, as the right kind of number") {
    App app;
    transform2d::install(app);
    lifetime::install(app);
    const auto& position = transform2d::info().components[0];
    const auto& life = lifetime::info().components[0];
    REQUIRE(position.read != nullptr);
    const Entity entity = app.world().spawn();
    const std::vector<ge::app::FieldValue> values = {{"x", 2.5}};
    position.insert(app.world(), entity, values);
    const auto x = position.read(app.world(), entity, "x");
    REQUIRE(x.has_value());
    CHECK(std::get<double>(*x) == 2.5);
    CHECK(!position.read(app.world(), entity, "z").has_value()); // no such field
    CHECK(!life.read(app.world(), entity, "ticks").has_value()); // no such component here
    life.insert(app.world(), entity, {});
    const auto ticks = life.read(app.world(), entity, "ticks");
    REQUIRE(ticks.has_value());
    CHECK(std::get<std::int64_t>(*ticks) == 60);
}

TEST_CASE("modules: settings are a resource the game can change") {
    App app;
    transform2d::install(app);
    REQUIRE(app.world().resource<transform2d::Params>() != nullptr);
    CHECK(app.world().resource<transform2d::Params>()->speed_scale == 1.0F);
    const ge::app::ModuleInfo& info = transform2d::info();
    REQUIRE(info.params.size() == 1U);
    CHECK(info.params[0].name == "speed_scale");
    CHECK(info.params[0].min == 0.0);
    CHECK(info.params[0].max == 10.0);
    REQUIRE(info.apply_params != nullptr);

    const Entity entity =
        app.world().spawn_with(transform2d::Position{}, transform2d::Velocity{60.0F, 0.0F});
    const std::vector<ge::app::FieldValue> values = {{"speed_scale", 0.5}};
    info.apply_params(app.world(), values);
    app.run_frames(60);
    const float x = app.world().get<transform2d::Position>(entity)->x;
    CHECK(x > 29.9F);
    CHECK(x < 30.1F);
    // A module without settings has nothing to apply.
    CHECK(lifetime::info().apply_params == nullptr);
    CHECK(lifetime::info().params.empty());
}

TEST_CASE("modules: scalars convert to the field's type") {
    using ge::app::Scalar;
    using ge::app::scalar_as;
    CHECK(scalar_as<float>(Scalar{2.5}) == 2.5F);
    CHECK(scalar_as<std::int32_t>(Scalar{std::int64_t{-4}}) == -4);
    CHECK(scalar_as<bool>(Scalar{true}));
    CHECK(scalar_as<std::uint32_t>(Scalar{true}) == 1U);
    CHECK(scalar_as<double>(Scalar{std::int64_t{3}}) == 3.0);
}

TEST_CASE("modules: action_control steers by the move actions, before the movement") {
    ge::app::App app;
    transform2d::install(app);
    ge::mod::action_control::install(app);
    CHECK(app.module_problems().empty());
    auto& world = app.world();
    const Entity pilot = world.spawn();
    const Entity rock = world.spawn();
    for (const Entity entity : {pilot, rock}) {
        CHECK(world.insert(entity, transform2d::Position{}));
        CHECK(world.insert(entity, transform2d::Velocity{}));
    }
    CHECK(world.insert(pilot, ge::mod::action_control::Controlled{60.0F}));

    auto* script = world.resource<ge::app::ScriptedInput>();
    script->add(0, "move_x", 1.0F);
    script->add(2, "move_x", 0.0F);
    script->add(2, "move_y", -0.5F);
    app.run_frames(1);
    // The velocity was set before the movement ran, so the first tick already moves.
    CHECK(world.get<transform2d::Position>(pilot)->x == 1.0F); // 60 units per second, 60 ticks
    app.run_frames(2);
    CHECK(world.get<transform2d::Velocity>(pilot)->x == 0.0F);
    CHECK(world.get<transform2d::Velocity>(pilot)->y == -30.0F);
    CHECK(world.get<transform2d::Position>(rock)->x == 0.0F); // not Controlled
}

TEST_CASE("modules: a field can be read and written by name through the module's info") {
    App app;
    transform2d::install(app);
    const Entity entity = app.world().spawn_with(transform2d::Position{1.0F, 2.0F});
    const ge::app::ComponentInfo& position = transform2d::info().components[0];
    REQUIRE(position.write != nullptr);
    CHECK(position.write(app.world(), entity, "y", ge::app::Scalar{7.5}));
    CHECK(app.world().get<transform2d::Position>(entity)->y == 7.5F);
    CHECK(app.world().get<transform2d::Position>(entity)->x == 1.0F);
    CHECK(!position.write(app.world(), entity, "nothing", ge::app::Scalar{1.0}));
    CHECK(!position.write(app.world(), app.world().spawn(), "x", ge::app::Scalar{1.0}));
    CHECK(ge::app::scalar_as<float>(*position.read(app.world(), entity, "y")) == 7.5F);
}
