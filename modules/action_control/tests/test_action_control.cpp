#include "ge/app/app.hpp"
#include "ge/app/input.hpp"
#include "ge/mod/action_control/action_control.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

namespace {

namespace action_control = ge::mod::action_control;
namespace transform2d = ge::mod::transform2d;

} // namespace

TEST_CASE("action_control: the actions set the velocity, an axis turned off is left alone") {
    ge::app::App app;
    transform2d::install(app);
    action_control::install(app);
    action_control::Controlled walker{50.0F};
    walker.vertical = false;
    const auto entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{}));
    CHECK(app.world().insert(entity, transform2d::Velocity{0.0F, 33.0F}));
    CHECK(app.world().insert(entity, walker));
    app.world().resource<ge::app::Actions>()->set("move_x", -1.0F);
    app.world().resource<ge::app::Actions>()->set("move_y", 1.0F);
    app.run_frames(1);
    const auto* velocity = app.world().get<transform2d::Velocity>(entity);
    REQUIRE(velocity != nullptr);
    CHECK(velocity->x == -50.0F);
    CHECK(velocity->y == 33.0F); // gravity or anything else keeps it
}
