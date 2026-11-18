#include "ge/app/app.hpp"
#include "ge/mod/screen_wrap/screen_wrap.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

namespace {

namespace screen_wrap = ge::mod::screen_wrap;
namespace transform2d = ge::mod::transform2d;

ge::ecs::Entity spawn(ge::app::App& app, float x, float y, screen_wrap::WrapArea area) {
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{x, y}));
    CHECK(app.world().insert(entity, area));
    return entity;
}

} // namespace

TEST_CASE("screen_wrap: an entity that leaves by one side comes back by the other") {
    ge::app::App app;
    transform2d::install(app);
    screen_wrap::install(app);
    const auto entity = spawn(app, -10.0F, 610.0F, screen_wrap::WrapArea{480.0F, 600.0F});
    app.run_frames(1);
    const auto* position = app.world().get<transform2d::Position>(entity);
    REQUIRE(position != nullptr);
    CHECK(position->x == 470.0F);
    CHECK(position->y == 10.0F);
}

TEST_CASE("screen_wrap: an axis turned off is not wrapped") {
    ge::app::App app;
    transform2d::install(app);
    screen_wrap::install(app);
    screen_wrap::WrapArea area{480.0F, 600.0F};
    area.wrap_y = false;
    const auto entity = spawn(app, 500.0F, -900.0F, area);
    app.run_frames(1);
    const auto* position = app.world().get<transform2d::Position>(entity);
    REQUIRE(position != nullptr);
    CHECK(position->x == 20.0F);
    CHECK(position->y == -900.0F);
}
