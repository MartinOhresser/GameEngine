#include "ge/app/app.hpp"
#include "ge/app/fields.hpp"
#include "ge/app/input.hpp"
#include "ge/app/lists.hpp"
#include "ge/app/runtime_lists.hpp"
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
        app.world().insert_resource(
            ge::app::ActionNames{{"up", "down", "accept", "pointer_x", "pointer_y", "click", "play",
                                  "quit", "left", "right", "help"}});
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
constexpr std::uint32_t left = 8;
constexpr std::uint32_t right = 9;

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

namespace {

/// Presses an action for a tick, then lets a tick go by, as a key does.
void tap(Scene& scene, const std::string& action, std::uint64_t tick) {
    auto* script = scene.app.world().resource<ge::app::ScriptedInput>();
    script->add(tick, action, 1.0F);
    script->add(tick + 1, action, 0.0F);
}

/// The number of the button that has the focus of the (only) menu.
std::uint32_t focused(Scene& scene) {
    std::uint32_t found = 0;
    scene.app.world().each<const ui::Menu>(
        [&found](ge::ecs::Entity, const ui::Menu& m) { found = m.focused; });
    return found;
}

ui::Menu menu_with_arrows() {
    ui::Menu menu = menu_with_keys();
    menu.left = left;
    menu.right = right;
    return menu;
}

ui::Button sized(std::uint32_t order, float width, float height) {
    ui::Button result = button(order, play);
    result.width = width;
    result.height = height;
    return result;
}

} // namespace

TEST_CASE("ui: buttons side by side are moved through with left and right, not up and down") {
    Scene scene;
    scene.spawn(menu_with_arrows());
    // A row of three at the same height, with a wide button above them.
    scene.spawn(transform2d::Position{100.0F, 0.0F}, sized(0, 300.0F, 30.0F));
    scene.spawn(transform2d::Position{40.0F, 60.0F}, sized(1, 56.0F, 36.0F));
    scene.spawn(transform2d::Position{110.0F, 62.0F}, sized(2, 56.0F, 36.0F)); // a little lower
    scene.spawn(transform2d::Position{180.0F, 60.0F}, sized(3, 56.0F, 36.0F));
    tap(scene, "down", 1); // to the row: the button nearest to the middle of the one above
    tap(scene, "right", 3);
    tap(scene, "right", 5); // the end of the row: round to its first
    tap(scene, "left", 7);  // and back to its last
    tap(scene, "up", 9);
    tap(scene, "down", 11);
    scene.app.run_frames(1);
    CHECK(focused(scene) == 0U);
    for (const std::uint32_t at : {2U, 3U, 1U, 3U, 0U, 2U}) {
        scene.app.run_frames(2);
        CHECK(focused(scene) == at);
    }
}

TEST_CASE("ui: up and down go to the button of the next row that is the nearest") {
    Scene scene;
    scene.spawn(menu_with_arrows());
    // A game's row, wide, above a bar of buttons (the launcher): down from the row lands on
    // the button below its middle, up from the far right one goes to the row.
    scene.spawn(transform2d::Position{205.0F, 124.0F}, sized(0, 330.0F, 40.0F));
    scene.spawn(transform2d::Position{68.0F, 506.0F}, sized(1, 56.0F, 36.0F));
    scene.spawn(transform2d::Position{214.0F, 506.0F}, sized(2, 100.0F, 36.0F));
    scene.spawn(transform2d::Position{320.0F, 506.0F}, sized(3, 100.0F, 36.0F));
    tap(scene, "down", 1);
    tap(scene, "right", 3);
    tap(scene, "up", 5);
    scene.app.run_frames(1);
    CHECK(focused(scene) == 0U);
    scene.app.run_frames(2);
    CHECK(focused(scene) == 2U); // nearest below x 205 is the one at 214
    scene.app.run_frames(2);
    CHECK(focused(scene) == 3U);
    scene.app.run_frames(2);
    CHECK(focused(scene) == 0U);
}

TEST_CASE("ui: a grid is moved through in both directions, and left stops at an end without wrap") {
    Scene scene;
    ui::Menu menu = menu_with_arrows();
    menu.wrap = false;
    scene.spawn(menu);
    for (std::uint32_t i = 0; i < 6; ++i) { // 3 columns, 2 rows
        const float column = static_cast<float>(i % 3U) * 50.0F;
        const float row = i < 3U ? 0.0F : 50.0F;
        scene.spawn(transform2d::Position{column, row}, sized(i, 40.0F, 40.0F));
    }
    tap(scene, "right", 1);
    tap(scene, "down", 3);
    tap(scene, "left", 5);
    tap(scene, "left", 7); // the left end: stays
    tap(scene, "down", 9); // the bottom: stays
    tap(scene, "up", 11);
    scene.app.run_frames(1);
    for (const std::uint32_t at : {1U, 4U, 3U, 3U, 3U, 0U}) {
        scene.app.run_frames(2);
        CHECK(focused(scene) == at);
    }
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

namespace {

constexpr std::uint32_t previous_page = 8;
constexpr std::uint32_t next_page = 9;

/// A list of `count` items shown by three rows, as a Pager of page_size 3 would, with a
/// button "play" under it that is not a row. Returns the rows' entities.
struct PagedScene {
    Scene scene;
    std::vector<ge::ecs::Entity> rows;
    ge::ecs::Entity pager;
    ge::ecs::Entity play_button;

    explicit PagedScene(std::size_t count) {
        scene.app.world().insert_resource(
            ge::app::ActionNames{{"up", "down", "accept", "pointer_x", "pointer_y", "click", "play",
                                  "quit", "previous", "next"}});
        scene.app.world().insert_resource(
            ge::app::ListNames{{{"runtime_list", {"games"}}, {"menu", {"main"}}}});
        std::vector<std::string> items;
        for (std::size_t i = 0; i < count; ++i) {
            items.push_back("game " + std::to_string(i));
        }
        scene.app.world().resource<ge::app::RuntimeLists>()->set("games", items);
        scene.spawn(menu_with_keys());
        ui::Pager paging;
        paging.page_size = 3;
        paging.previous = previous_page;
        paging.next = next_page;
        pager = scene.spawn(paging);
        for (std::uint32_t row = 0; row < 3; ++row) {
            ui::Row line;
            line.row = row;
            rows.push_back(scene.spawn(transform2d::Position{0.0F, 40.0F * static_cast<float>(row)},
                                       sprite2d::Sprite{}, text2d::Text{}, text2d::Dynamic{}, line,
                                       button(row, play)));
        }
        play_button =
            scene.spawn(transform2d::Position{0.0F, 200.0F}, sprite2d::Sprite{}, button(3, quit));
    }

    [[nodiscard]] std::uint32_t item_of(std::size_t row) const {
        return scene.app.world().get<ui::Row>(rows[row])->item;
    }
    [[nodiscard]] bool enabled(std::size_t row) const {
        return scene.app.world().get<ui::Button>(rows[row])->enabled;
    }
    void press(const char* action) {
        scene.app.world().resource<ge::app::Actions>()->raise(action);
        scene.app.run_frames(2);
    }
};

} // namespace

TEST_CASE("ui: a pager gives each row its item, and the buttons turn the pages") {
    PagedScene paged(5);
    paged.scene.app.run_frames(1);
    CHECK(paged.item_of(0) == 0U);
    CHECK(paged.item_of(2) == 2U);
    CHECK(paged.enabled(2));
    const auto* dynamic = paged.scene.app.world().get<text2d::Dynamic>(paged.rows[1]);
    CHECK(dynamic->list == 0U);
    CHECK(dynamic->index == 1U);
    CHECK(paged.scene.app.world().get<ui::Pager>(paged.pager)->pages == 2U);
}

TEST_CASE("ui: the buttons of a pager turn the pages, and rows without an item go off") {
    PagedScene paged(5);
    paged.scene.app.run_frames(1);
    paged.press("next");
    CHECK(paged.scene.app.world().get<ui::Pager>(paged.pager)->page == 1U);
    CHECK(paged.item_of(0) == 3U);
    CHECK(paged.item_of(1) == 4U);
    CHECK(paged.enabled(1));
    CHECK(!paged.enabled(2)); // no sixth item: the row is switched off
    paged.press("next");      // the last page is the end
    CHECK(paged.scene.app.world().get<ui::Pager>(paged.pager)->page == 1U);
    paged.press("previous");
    paged.press("previous");
    CHECK(paged.scene.app.world().get<ui::Pager>(paged.pager)->page == 0U);
    CHECK(paged.enabled(2));
}

TEST_CASE("ui: a list that changes size changes the pages, and an empty list has one") {
    PagedScene paged(7);
    paged.scene.app.run_frames(1);
    CHECK(paged.scene.app.world().get<ui::Pager>(paged.pager)->pages == 3U);
    paged.press("next");
    paged.press("next");
    CHECK(paged.item_of(0) == 6U);
    paged.scene.app.world().resource<ge::app::RuntimeLists>()->set("games", {"only"});
    paged.scene.app.run_frames(2);
    const auto* pager = paged.scene.app.world().get<ui::Pager>(paged.pager);
    CHECK(pager->pages == 1U);
    CHECK(pager->page == 0U); // pulled back inside the list
    CHECK(paged.enabled(0));
    CHECK(!paged.enabled(1));
}

TEST_CASE("ui: the focus skips the rows that have no item") {
    PagedScene paged(2); // rows 0 and 1 are shown, row 2 is off; button 3 is "quit"
    auto* script = paged.scene.app.world().resource<ge::app::ScriptedInput>();
    script->add(1, "down", 1.0F);
    script->add(2, "down", 0.0F);
    script->add(3, "down", 1.0F);
    script->add(4, "down", 0.0F);
    script->add(5, "down", 1.0F);
    script->add(6, "down", 0.0F);
    const auto* menu_of = [&paged]() {
        const ui::Menu* found = nullptr;
        paged.scene.app.world().each<const ui::Menu>(
            [&found](ge::ecs::Entity, const ui::Menu& m) { found = &m; });
        return found;
    }();
    REQUIRE(menu_of != nullptr);
    paged.scene.app.run_frames(2); // after down: row 1
    CHECK(menu_of->focused == 1U);
    paged.scene.app.run_frames(2); // row 2 is off: the next is the button 3
    CHECK(menu_of->focused == 3U);
}

TEST_CASE("ui: the focused row is the selection of the list, and it stays when the focus leaves") {
    PagedScene paged(5);
    const auto* lists = paged.scene.app.world().resource<ge::app::RuntimeLists>();
    paged.scene.app.run_frames(1);
    CHECK(lists->selected("games") == 0U); // the first button has the focus
    auto* script = paged.scene.app.world().resource<ge::app::ScriptedInput>();
    script->add(2, "down", 1.0F);
    script->add(3, "down", 0.0F);
    paged.scene.app.run_frames(3);
    CHECK(lists->selected("games") == 1U);
    paged.press("next"); // the page turns: the focused row now shows item 4
    CHECK(lists->selected("games") == 4U);
    script->add(8, "down", 1.0F);
    script->add(9, "down", 0.0F);
    paged.scene.app.run_frames(4); // the focus skips row 2 (off) and lands on the button
    std::uint32_t focused = 99;
    paged.scene.app.world().each<const ui::Menu>(
        [&focused](ge::ecs::Entity, const ui::Menu& m) { focused = m.focused; });
    CHECK(focused == 3U);
    CHECK(lists->selected("games") == 4U); // still the last row that had it
}
