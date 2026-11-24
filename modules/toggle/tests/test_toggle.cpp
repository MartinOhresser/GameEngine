#include "ge/app/app.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/mod/tag/tag.gen.hpp"
#include "ge/mod/toggle/toggle.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

#include <cstdint>

namespace {

namespace tag = ge::mod::tag;
namespace toggle = ge::mod::toggle;
namespace transform2d = ge::mod::transform2d;

constexpr std::uint32_t map_tag = 4U;

struct Screen {
    ge::app::App app;
    std::uint64_t tick = 0;
    ge::ecs::Entity map;
    ge::ecs::Entity shown;
    ge::ecs::Entity hidden;

    explicit Screen(bool hold) {
        transform2d::install(app);
        tag::install(app);
        toggle::install(app);
        CHECK(app.module_problems().empty());
        CHECK(app.schedule().problems().empty());
        app.world().insert_resource(ge::app::ActionNames{{"map"}});
        map = app.world().spawn();
        CHECK(app.world().insert(map, toggle::Toggle{0U, hold}));
        CHECK(app.world().insert(map, tag::Tag{map_tag}));
        shown = app.world().spawn();
        CHECK(app.world().insert(shown, toggle::ShownBy{map_tag}));
        hidden = app.world().spawn();
        CHECK(app.world().insert(hidden, toggle::ShownBy{map_tag, true}));
    }
    void press(std::uint64_t ticks) {
        auto* input = app.world().resource<ge::app::ScriptedInput>();
        input->add(tick, "map", 1.0F);
        input->add(tick + ticks, "map", 0.0F);
    }
    void run(std::uint64_t ticks) {
        app.run_frames(static_cast<std::uint32_t>(ticks));
        tick += ticks;
    }
    bool on() { return app.world().get<toggle::Toggle>(map)->on; }
    bool visible(ge::ecs::Entity entity) { return !app.world().has<transform2d::Hidden>(entity); }
};

} // namespace

TEST_CASE("toggle: a press switches it on, another off; what follows it shows and hides") {
    Screen screen(false);
    screen.run(1);
    CHECK(!screen.visible(screen.shown));
    CHECK(screen.visible(screen.hidden));
    screen.press(3);
    screen.run(5);
    CHECK(screen.on());
    CHECK(screen.visible(screen.shown));
    CHECK(!screen.visible(screen.hidden));
    screen.press(3);
    screen.run(5);
    CHECK(!screen.on());
    CHECK(!screen.visible(screen.shown));
    CHECK(screen.app.world().get<toggle::Toggle>(screen.map)->times == 1U);
}

TEST_CASE("toggle: with hold it is on only while the action is held") {
    Screen screen(true);
    screen.press(10);
    screen.run(5);
    CHECK(screen.on());
    screen.run(10);
    CHECK(!screen.on());
}
