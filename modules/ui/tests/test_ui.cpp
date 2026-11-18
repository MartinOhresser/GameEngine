#include "ge/app/app.hpp"
#include "ge/app/fields.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/app/viewport.hpp"
#include "ge/mod/sprite2d/sprite2d.gen.hpp"
#include "ge/mod/text2d/text2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"
#include "ge/mod/ui/ui.gen.hpp"

#include "mini_test.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace {

namespace sprite2d = ge::mod::sprite2d;
namespace text2d = ge::mod::text2d;
namespace transform2d = ge::mod::transform2d;
namespace ui = ge::mod::ui;

struct Scene {
    ge::app::App app;

    Scene() {
        transform2d::install(app);
        sprite2d::install(app);
        text2d::install(app);
        ui::install(app);
        CHECK(app.module_problems().empty());
        CHECK(app.schedule().problems().empty());
        // The game's actions, as a game's manifest would list them.
        app.world().insert_resource(ge::app::ActionNames{
            {"up", "down", "accept", "pointer_x", "pointer_y", "click", "play", "quit"}});
    }

    template <typename... Components>
    ge::ecs::Entity spawn(Components... components) {
        const ge::ecs::Entity entity = app.world().spawn();
        const bool all_inserted = (app.world().insert(entity, components) && ...);
        CHECK(all_inserted);
        return entity;
    }

    [[nodiscard]] const transform2d::Position* position(ge::ecs::Entity entity) const {
        return app.world().get<transform2d::Position>(entity);
    }
};

constexpr std::uint32_t up = 0;
constexpr std::uint32_t down = 1;
constexpr std::uint32_t accept = 2;
constexpr std::uint32_t pointer_x = 3;
constexpr std::uint32_t pointer_y = 4;
constexpr std::uint32_t click = 5;
constexpr std::uint32_t play = 6;
constexpr std::uint32_t quit = 7;

ui::Menu menu_with_keys() {
    ui::Menu menu;
    menu.up = up;
    menu.down = down;
    menu.accept = accept;
    return menu;
}

ui::Button button(std::uint32_t order, std::uint32_t action) {
    ui::Button result;
    result.order = order;
    result.action = action;
    return result;
}

/// Where the field sits in the table of fields: what a game file's name becomes.
std::uint32_t field_index(const ge::app::App& app, const std::string& name) {
    const auto* table = app.world().resource<ge::app::FieldTable>();
    const auto found = std::find_if(table->entries.begin(), table->entries.end(),
                                    [&name](const auto& entry) { return entry.name == name; });
    CHECK(found != table->entries.end());
    return static_cast<std::uint32_t>(found - table->entries.begin());
}

} // namespace

TEST_CASE("ui: an anchored entity follows the window's edges") {
    Scene scene;
    ui::Anchor corner;
    corner.anchor_x = 1.0F;
    corner.offset_x = -10.0F;
    corner.offset_y = 10.0F;
    ui::Anchor middle;
    middle.anchor_x = 0.5F;
    middle.anchor_y = 0.5F;
    const auto top_right = scene.spawn(transform2d::Position{}, corner);
    const auto centre = scene.spawn(transform2d::Position{}, middle);
    scene.app.run_frames(1);
    CHECK(scene.position(top_right)->x == 790.0F); // 800 by 600 unless the game says otherwise
    CHECK(scene.position(top_right)->y == 10.0F);
    CHECK(scene.position(centre)->x == 400.0F);
    CHECK(scene.position(centre)->y == 300.0F);

    scene.app.world().resource<ge::app::Viewport>()->width = 320U;
    scene.app.world().resource<ge::app::Viewport>()->height = 180U;
    scene.app.run_frames(1);
    CHECK(scene.position(top_right)->x == 310.0F);
    CHECK(scene.position(centre)->y == 90.0F);
}

TEST_CASE("ui: a grid places its cells in rows and columns, and a column is a grid of one") {
    Scene scene;
    ui::Grid grid;
    grid.group = 1;
    grid.columns = 3;
    grid.cell_width = 20.0F;
    grid.cell_height = 10.0F;
    grid.spacing_x = 4.0F;
    grid.spacing_y = 2.0F;
    scene.spawn(transform2d::Position{100.0F, 50.0F}, grid);
    std::vector<ge::ecs::Entity> cells;
    for (std::uint32_t i = 0; i < 5; ++i) {
        cells.push_back(scene.spawn(transform2d::Position{}, ui::Cell{1, i}));
    }
    const auto elsewhere = scene.spawn(transform2d::Position{7.0F, 7.0F}, ui::Cell{2, 0});
    scene.app.run_frames(1);
    CHECK(scene.position(cells[0])->x == 100.0F);
    CHECK(scene.position(cells[0])->y == 50.0F);
    CHECK(scene.position(cells[2])->x == 148.0F); // two columns of 24
    CHECK(scene.position(cells[3])->x == 100.0F); // back to the first column...
    CHECK(scene.position(cells[3])->y == 62.0F);  // ...one row of 12 lower
    CHECK(scene.position(cells[4])->x == 124.0F);
    CHECK(scene.position(elsewhere)->x == 7.0F); // another group: no grid, not moved
}

TEST_CASE("ui: a bar fills with its value, from its pivot") {
    Scene scene;
    ui::Bar bar;
    bar.value = 25.0F;
    bar.max = 100.0F;
    bar.length = 80.0F;
    bar.thickness = 6.0F;
    const auto horizontal = scene.spawn(transform2d::Position{}, sprite2d::Sprite{}, bar);
    bar.vertical = true;
    bar.value = 500.0F; // above max: full
    const auto vertical = scene.spawn(transform2d::Position{}, sprite2d::Sprite{}, bar);
    scene.app.run_frames(1);
    const auto* flat = scene.app.world().get<sprite2d::Sprite>(horizontal);
    CHECK(flat->width == 20.0F);
    CHECK(flat->height == 6.0F);
    const auto* tall = scene.app.world().get<sprite2d::Sprite>(vertical);
    CHECK(tall->width == 6.0F);
    CHECK(tall->height == 80.0F);
}

TEST_CASE("ui: a text and a bar show a number of the game through a tag") {
    Scene scene;
    // Any numeric field can be shown; the x of a Position is at hand in every game.
    const auto hero = scene.spawn(transform2d::Position{12.0F, 0.0F}, ui::Tag{1});
    scene.spawn(transform2d::Position{99.0F, 0.0F}, ui::Tag{2}); // another tag, another number

    ui::Bind bind;
    bind.source = 1;
    bind.field = field_index(scene.app, "transform2d.Position.x");
    ui::Bar bar;
    bar.max = 24.0F;
    bar.length = 48.0F;
    const auto label = scene.spawn(transform2d::Position{}, text2d::Text{}, bind);
    const auto gauge = scene.spawn(transform2d::Position{}, sprite2d::Sprite{}, bar, bind);
    scene.app.run_frames(1);
    CHECK(scene.app.world().get<text2d::Text>(label)->value == 12.0F);
    CHECK(scene.app.world().get<ui::Bar>(gauge)->value == 12.0F);
    CHECK(scene.app.world().get<sprite2d::Sprite>(gauge)->width == 24.0F); // half of 48

    scene.app.world().get<transform2d::Position>(hero)->x = 24.0F;
    scene.app.run_frames(1);
    CHECK(scene.app.world().get<text2d::Text>(label)->value == 24.0F);

    ui::Bind nobody = bind;
    nobody.source = 5; // nothing has that tag: the number is 0
    const auto empty = scene.spawn(transform2d::Position{}, text2d::Text{}, nobody);
    scene.app.run_frames(1);
    CHECK(scene.app.world().get<text2d::Text>(empty)->value == 0.0F);
}

TEST_CASE("ui: the keys move the focus through the buttons and accept raises the action") {
    Scene scene;
    scene.spawn(menu_with_keys());
    ui::Button first = button(0, play);
    first.normal_frame = 1;
    first.focus_frame = 2;
    const auto start = scene.spawn(transform2d::Position{0.0F, 0.0F}, sprite2d::Sprite{}, first);
    const auto leave =
        scene.spawn(transform2d::Position{0.0F, 40.0F}, sprite2d::Sprite{}, button(1, quit));
    auto* script = scene.app.world().resource<ge::app::ScriptedInput>();
    script->add(1, "down", 1.0F);
    script->add(2, "down", 0.0F);
    script->add(3, "accept", 1.0F);
    script->add(4, "accept", 0.0F);

    scene.app.run_frames(1);
    CHECK(scene.app.world().get<sprite2d::Sprite>(start)->frame == 2U); // the first has the focus
    CHECK(scene.app.world().get<sprite2d::Sprite>(leave)->frame == 0U);
    scene.app.run_frames(1); // tick 1: down
    CHECK(scene.app.world().get<sprite2d::Sprite>(start)->frame == 1U);
    CHECK(scene.app.world().get<sprite2d::Sprite>(leave)->frame == 0U); // its normal_frame is 0
    const auto* actions = scene.app.world().resource<ge::app::Actions>();
    scene.app.run_frames(2);       // ticks 2 and 3: accept goes down at tick 3
    CHECK(!actions->down("quit")); // raised for the tick after
    scene.app.run_frames(1);       // tick 4
    CHECK(actions->pressed("quit"));
    CHECK(!actions->down("play"));
    scene.app.run_frames(1); // tick 5: the press lasted one tick only
    CHECK(!actions->down("quit"));
}

TEST_CASE("ui: the focus wraps round, or stops at the ends") {
    Scene scene;
    ui::Menu menu = menu_with_keys();
    const auto controller = scene.spawn(menu);
    scene.spawn(transform2d::Position{}, button(0, play));
    scene.spawn(transform2d::Position{}, button(1, quit));
    auto* script = scene.app.world().resource<ge::app::ScriptedInput>();
    script->add(0, "up", 1.0F); // up from the first button
    script->add(1, "up", 0.0F);
    scene.app.run_frames(1);
    CHECK(scene.app.world().get<ui::Menu>(controller)->focused == 1U); // wrapped to the last

    Scene stopped;
    menu.wrap = false;
    const auto fixed = stopped.spawn(menu);
    stopped.spawn(transform2d::Position{}, button(0, play));
    stopped.spawn(transform2d::Position{}, button(1, quit));
    stopped.app.world().resource<ge::app::ScriptedInput>()->add(0, "up", 1.0F);
    stopped.app.run_frames(1);
    CHECK(stopped.app.world().get<ui::Menu>(fixed)->focused == 0U);
}

TEST_CASE("ui: the pointer focuses the button it moves over and a click presses it") {
    Scene scene;
    ui::Menu menu = menu_with_keys();
    menu.use_pointer = true;
    menu.pointer_x = pointer_x;
    menu.pointer_y = pointer_y;
    menu.click = click;
    const auto controller = scene.spawn(menu);
    ui::Button wide = button(0, play);
    wide.width = 100.0F;
    wide.height = 30.0F;
    scene.spawn(transform2d::Position{200.0F, 100.0F}, wide);
    wide.order = 1;
    wide.action = quit;
    scene.spawn(transform2d::Position{200.0F, 160.0F}, wide);
    auto* script = scene.app.world().resource<ge::app::ScriptedInput>();
    script->add(0, "pointer_x", 190.0F);
    script->add(0, "pointer_y", 165.0F); // over the second button
    script->add(2, "click", 1.0F);
    script->add(3, "click", 0.0F);
    scene.app.run_frames(1);
    CHECK(scene.app.world().get<ui::Menu>(controller)->focused == 1U);
    scene.app.run_frames(2); // tick 2: the click
    const auto* actions = scene.app.world().resource<ge::app::Actions>();
    CHECK(!actions->down("quit"));
    scene.app.run_frames(1); // tick 3: its action is pressed
    CHECK(actions->pressed("quit"));
    // A click away from every button does nothing.
    script->add(5, "pointer_y", 400.0F);
    script->add(6, "click", 1.0F);
    scene.app.run_frames(5);
    CHECK(!actions->down("play"));
    CHECK(!actions->down("quit"));
}
