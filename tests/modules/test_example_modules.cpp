// The example modules, installed into an app the way a generated game would.

#include "ge/app/app.hpp"
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
          std::vector<std::string>{"transform2d.integrate"});
}

TEST_CASE("modules: the generated info matches module.json") {
    const ge::app::ModuleInfo& info = transform2d::info();
    CHECK(info.name == "transform2d");
    CHECK(info.version == "0.1.0");
    CHECK(info.depends.empty());
    REQUIRE(info.components.size() == 2U);
    CHECK(info.components[0].name == "Position");
    CHECK(info.components[0].replicable);
    REQUIRE(info.components[0].fields.size() == 2U);
    CHECK(info.components[0].fields[1].name == "y");
    CHECK(info.components[0].fields[1].type == "f32");
    REQUIRE(info.systems.size() == 1U);
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
          (std::vector<std::string>{"transform2d.integrate", "screen_wrap.wrap"}));

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
