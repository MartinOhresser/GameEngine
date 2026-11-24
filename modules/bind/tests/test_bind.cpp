#include "ge/app/app.hpp"
#include "ge/app/fields.hpp"
#include "ge/mod/bind/bind.gen.hpp"
#include "ge/mod/tag/tag.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace {

namespace bind = ge::mod::bind;
namespace tag = ge::mod::tag;
namespace transform2d = ge::mod::transform2d;

ge::app::App game() {
    ge::app::App app;
    transform2d::install(app);
    tag::install(app);
    bind::install(app);
    CHECK(app.module_problems().empty());
    CHECK(app.schedule().problems().empty());
    return app;
}

std::uint32_t field_index(const ge::app::App& app, const std::string& name) {
    const auto* table = app.world().resource<ge::app::FieldTable>();
    const auto found = std::find_if(table->entries.begin(), table->entries.end(),
                                    [&name](const auto& entry) { return entry.name == name; });
    CHECK(found != table->entries.end());
    return static_cast<std::uint32_t>(found - table->entries.begin());
}

bool near(float a, float b) { return std::abs(a - b) < 0.01F; }

} // namespace

TEST_CASE("bind: a number of one entity follows a number of another, through a curve") {
    ge::app::App app = game();
    const ge::ecs::Entity source = app.world().spawn();
    CHECK(app.world().insert(source, transform2d::Position{25.0F, 0.0F}));
    CHECK(app.world().insert(source, tag::Tag{1U}));
    const ge::ecs::Entity target = app.world().spawn();
    CHECK(app.world().insert(target, transform2d::Position{}));
    CHECK(app.world().insert(target, tag::Tag{2U}));
    // x 100 gives 0, x 0 gives 1, squared: the target's y grows late, from 10 to 30.
    const ge::ecs::Entity link = app.world().spawn();
    CHECK(app.world().insert(link, bind::Bind{field_index(app, "transform2d.Position.x"), 1U,
                                              field_index(app, "transform2d.Position.y"), 2U,
                                              100.0F, 0.0F, 10.0F, 30.0F, 2.0F}));
    app.run_frames(1);
    // 25 is three quarters of the way from 100 to 0: 0.75 squared is 0.5625, 10 + 20 × 0.5625.
    CHECK(near(app.world().get<transform2d::Position>(target)->y, 21.25F));
    app.world().get<transform2d::Position>(source)->x = 500.0F;
    app.run_frames(1);
    CHECK(near(app.world().get<transform2d::Position>(target)->y, 10.0F));
}

TEST_CASE("bind: without tags it works on its own entity; a missing one writes nothing") {
    ge::app::App app = game();
    const ge::ecs::Entity self = app.world().spawn();
    CHECK(app.world().insert(self, transform2d::Position{0.5F, 7.0F}));
    bind::Bind same;
    same.source = field_index(app, "transform2d.Position.x");
    same.target = field_index(app, "transform2d.Velocity.x");
    same.out_high = 100.0F;
    CHECK(app.world().insert(self, same));
    app.run_frames(1);
    CHECK(!app.world().has<transform2d::Velocity>(self)); // no Velocity: nothing to write
    CHECK(app.world().insert(self, transform2d::Velocity{}));
    app.run_frames(1);
    CHECK(near(app.world().get<transform2d::Velocity>(self)->x, 50.0F));
    bind::Bind lost = same;
    lost.from = 9U; // nobody answers to it
    CHECK(app.world().insert(self, lost));
    app.world().get<transform2d::Velocity>(self)->x = 3.0F;
    app.run_frames(1);
    CHECK(app.world().get<transform2d::Velocity>(self)->x == 3.0F);
}
