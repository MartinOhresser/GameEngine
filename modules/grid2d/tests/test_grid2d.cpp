#include "ge/app/app.hpp"
#include "ge/app/fields.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/app/spawn.hpp"
#include "ge/app/viewport.hpp"
#include "ge/core/random.hpp"
#include "ge/mod/camera2d/camera2d.gen.hpp"
#include "ge/mod/grid2d/cells.hpp"
#include "ge/mod/grid2d/grid2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace {

namespace camera2d = ge::mod::camera2d;
namespace grid2d = ge::mod::grid2d;
namespace transform2d = ge::mod::transform2d;

// A board of 4 columns by 3 rows of 10 by 10 units, centred on (100, 50): its cells are at
// x = 85, 95, 105, 115 and y = 40, 50, 60.
struct Scene {
    ge::app::App app;

    Scene() {
        transform2d::install(app);
        camera2d::install(app);
        grid2d::install(app);
        CHECK(app.module_problems().empty());
        CHECK(app.schedule().problems().empty());
        app.world().insert_resource(
            ge::app::ActionNames{{"pointer_x", "pointer_y", "click", "cell", "grow", "left",
                                  "right", "up", "down", "accept"}});
        app.world().insert_resource(ge::Random{7});
        const auto board = app.world().spawn();
        CHECK(app.world().insert(board, transform2d::Position{100.0F, 50.0F}));
        grid2d::Grid grid;
        grid.columns = 4;
        grid.rows = 3;
        grid.cell_width = 10.0F;
        grid.cell_height = 10.0F;
        CHECK(app.world().insert(board, grid));
    }

    ge::ecs::Entity piece(std::int32_t col, std::int32_t row, float glide = 0.0F) {
        const auto entity = app.world().spawn();
        CHECK(app.world().insert(entity, transform2d::Position{0.0F, 0.0F}));
        grid2d::GridPos at;
        at.col = col;
        at.row = row;
        at.glide = glide;
        CHECK(app.world().insert(entity, at));
        return entity;
    }

    [[nodiscard]] std::uint32_t field(const std::string& name) const {
        const auto* table = app.world().resource<ge::app::FieldTable>();
        const auto found = std::find_if(table->entries.begin(), table->entries.end(),
                                        [&name](const auto& entry) { return entry.name == name; });
        CHECK(found != table->entries.end());
        return static_cast<std::uint32_t>(found - table->entries.begin());
    }

    ge::ecs::Entity zone(std::int32_t col, std::int32_t row, std::uint32_t columns,
                         std::uint32_t rows, bool matching = false) {
        const auto entity = app.world().spawn();
        grid2d::Zone area;
        area.col = col;
        area.row = row;
        area.columns = columns;
        area.rows = rows;
        area.action = 3;
        if (matching) {
            area.match_field = field("grid2d.GridPos.glide");
            area.match_value = 0.5F;
        }
        CHECK(app.world().insert(entity, area));
        return entity;
    }

    [[nodiscard]] grid2d::Zone area(ge::ecs::Entity entity) const {
        return *app.world().get<grid2d::Zone>(entity);
    }

    [[nodiscard]] transform2d::Position where(ge::ecs::Entity entity) const {
        return *app.world().get<transform2d::Position>(entity);
    }
};

void script(Scene& scene, std::uint64_t tick, const char* action, float value) {
    scene.app.world().resource<ge::app::ScriptedInput>()->add(tick, action, value);
}

} // namespace

TEST_CASE("grid2d: an entity on a cell is put at the centre of that cell") {
    Scene scene;
    const auto top_left = scene.piece(0, 0);
    const auto middle = scene.piece(2, 1);
    const auto off_board = scene.piece(9, 9);
    scene.app.run_frames(1);
    CHECK(scene.where(top_left).x == 85.0F);
    CHECK(scene.where(top_left).y == 40.0F);
    CHECK(scene.where(middle).x == 105.0F);
    CHECK(scene.where(middle).y == 50.0F);
    CHECK(scene.where(off_board).x == 0.0F); // left where it was
}

TEST_CASE("grid2d: with glide an entity moves to its cell smoothly and lands on it") {
    Scene scene;
    const auto piece = scene.piece(3, 2, 0.5F);
    scene.app.world().get<transform2d::Position>(piece)->x = 115.0F;
    scene.app.world().get<transform2d::Position>(piece)->y = 60.0F;
    scene.app.run_frames(1);
    CHECK(scene.where(piece).x == 115.0F); // already there
    scene.app.world().get<grid2d::GridPos>(piece)->col = 0;
    scene.app.run_frames(1);
    CHECK(scene.where(piece).x == 100.0F); // half of the 30 units
    scene.app.run_frames(12);
    CHECK(scene.where(piece).x == 85.0F);
}

TEST_CASE("grid2d: from_position takes the cell an entity is on, once") {
    Scene scene;
    const auto piece = scene.piece(0, 0);
    scene.app.world().get<transform2d::Position>(piece)->x = 114.0F;
    scene.app.world().get<transform2d::Position>(piece)->y = 41.0F;
    scene.app.world().get<grid2d::GridPos>(piece)->from_position = true;
    scene.app.run_frames(1);
    CHECK(scene.app.world().get<grid2d::GridPos>(piece)->col == 3);
    CHECK(scene.app.world().get<grid2d::GridPos>(piece)->row == 0);
    CHECK(!scene.app.world().get<grid2d::GridPos>(piece)->from_position);
    CHECK(scene.where(piece).x == 115.0F); // and it is at the centre of that cell
}

TEST_CASE("grid2d: the layout of a board says where its cells are") {
    Scene scene;
    scene.app.run_frames(1);
    const auto layout = grid2d::layout_of(scene.app.world(), 0);
    REQUIRE(layout.has_value());
    CHECK(layout->columns == 4);
    CHECK(layout->contains(3, 2));
    CHECK(!layout->contains(4, 0));
    CHECK(layout->col_at(105.0F) == 2);
    CHECK(layout->row_at(40.0F) == 0);
    CHECK(!grid2d::layout_of(scene.app.world(), 5).has_value());
}

TEST_CASE("grid2d: who sits on each cell is known, and a Disabled entity does not count") {
    Scene scene;
    const auto a = scene.piece(1, 1);
    const auto gone = scene.piece(2, 2);
    CHECK(scene.app.world().insert(gone, transform2d::Disabled{}));
    scene.app.run_frames(1);
    const auto layout = grid2d::layout_of(scene.app.world(), 0);
    REQUIRE(layout.has_value());
    const auto cells = grid2d::occupancy(scene.app.world(), *layout);
    REQUIRE(cells.size() == 12U);
    CHECK(cells[layout->index(1, 1)] == a);
    CHECK(cells[layout->index(2, 2)].is_null());
}

namespace {

ge::ecs::Entity add_cursor(Scene& scene) {
    const auto cursor = scene.app.world().spawn();
    grid2d::GridCursor follow;
    follow.pointer_x = 0;
    follow.pointer_y = 1;
    follow.click = 2;
    follow.action = 3;
    CHECK(scene.app.world().insert(cursor, follow));
    return cursor;
}

} // namespace

TEST_CASE("grid2d: the cursor says which cell the pointer is over, and a click raises its action") {
    Scene scene;
    const auto cursor = add_cursor(scene);
    script(scene, 1, "pointer_x", 105.0F);
    script(scene, 1, "pointer_y", 50.0F);
    scene.app.run_frames(2);
    const auto* now = scene.app.world().get<grid2d::GridCursor>(cursor);
    CHECK(now->over);
    CHECK(now->col == 2);
    CHECK(now->row == 1);
    CHECK(!now->clicked);
    script(scene, 3, "click", 1.0F);
    scene.app.run_frames(2);
    CHECK(scene.app.world().get<grid2d::GridCursor>(cursor)->clicked);
    scene.app.run_frames(1);
    CHECK(scene.app.world().resource<ge::app::Actions>()->pressed("cell"));
}

TEST_CASE("grid2d: a pointer off the board is over nothing, and a click there does nothing") {
    Scene scene;
    const auto cursor = add_cursor(scene);
    script(scene, 0, "pointer_x", 500.0F);
    script(scene, 0, "pointer_y", 50.0F);
    script(scene, 1, "click", 1.0F);
    scene.app.run_frames(4);
    CHECK(!scene.app.world().get<grid2d::GridCursor>(cursor)->over);
    CHECK(!scene.app.world().resource<ge::app::Actions>()->pressed("cell"));
}

TEST_CASE("grid2d: the cursor reads the pointer through the camera") {
    Scene scene;
    const auto camera = scene.app.world().spawn();
    CHECK(scene.app.world().insert(camera, transform2d::Position{100.0F, 50.0F}));
    CHECK(scene.app.world().insert(camera, camera2d::Camera{2.0F}));
    scene.app.world().resource<ge::app::Viewport>()->width = 800U;
    scene.app.world().resource<ge::app::Viewport>()->height = 600U;
    const auto cursor = add_cursor(scene);
    // The middle of the window is the camera's point (100, 50); 40 pixels to the right is
    // 20 world units: x = 120, which is past the 4th column (x from 80 to 120): off the board.
    script(scene, 0, "pointer_x", 440.0F);
    script(scene, 0, "pointer_y", 300.0F);
    scene.app.run_frames(2);
    CHECK(!scene.app.world().get<grid2d::GridCursor>(cursor)->over);
    script(scene, 2, "pointer_x", 430.0F); // x = 115: the 4th column
    scene.app.run_frames(2);
    CHECK(scene.app.world().get<grid2d::GridCursor>(cursor)->over);
    CHECK(scene.app.world().get<grid2d::GridCursor>(cursor)->col == 3);
}

TEST_CASE("grid2d: a Disabled cursor does nothing") {
    Scene scene;
    const auto cursor = add_cursor(scene);
    CHECK(scene.app.world().insert(cursor, transform2d::Disabled{}));
    script(scene, 0, "pointer_x", 105.0F);
    script(scene, 0, "pointer_y", 50.0F);
    script(scene, 1, "click", 1.0F);
    scene.app.run_frames(4);
    CHECK(!scene.app.world().get<grid2d::GridCursor>(cursor)->over);
}

TEST_CASE("grid2d: SpawnOnEmpty creates entities only on empty cells, at their centre") {
    Scene scene;
    // 12 cells; all but (3, 2) are taken.
    for (std::int32_t row = 0; row < 3; ++row) {
        for (std::int32_t col = 0; col < 4; ++col) {
            if (col != 3 || row != 2) {
                static_cast<void>(scene.piece(col, row));
            }
        }
    }
    grid2d::SpawnOnEmpty spawner;
    spawner.trigger = 4;
    spawner.prefab = 7;
    spawner.count = 3; // more than there is room for
    const auto entity = scene.app.world().spawn();
    CHECK(scene.app.world().insert(entity, spawner));
    auto* input = scene.app.world().resource<ge::app::ScriptedInput>();
    input->add(0, "grow", 1.0F);
    input->add(1, "grow", 0.0F);
    std::vector<ge::app::SpawnRequest> made;
    scene.app.set_prefab_spawner(
        [&made](ge::app::App&, const ge::app::SpawnRequest& request) { made.push_back(request); });
    scene.app.run_frames(2);
    REQUIRE(made.size() == 1U);
    CHECK(made[0].prefab == 7U);
    CHECK(made[0].x == 115.0F);
    CHECK(made[0].y == 60.0F);
}

TEST_CASE("grid2d: SpawnOnEmpty makes the other prefab with the chance it is given") {
    Scene scene;
    grid2d::SpawnOnEmpty spawner;
    spawner.trigger = 4;
    spawner.prefab = 1;
    spawner.alt_prefab = 2;
    spawner.alt_percent = 100;
    spawner.count = 5;
    const auto entity = scene.app.world().spawn();
    CHECK(scene.app.world().insert(entity, spawner));
    std::vector<ge::app::SpawnRequest> made;
    scene.app.set_prefab_spawner(
        [&made](ge::app::App&, const ge::app::SpawnRequest& request) { made.push_back(request); });
    scene.app.world().resource<ge::app::ScriptedInput>()->add(0, "grow", 1.0F);
    scene.app.world().resource<ge::app::ScriptedInput>()->add(1, "grow", 0.0F);
    scene.app.run_frames(3);
    REQUIRE(made.size() == 5U);
    bool all_alternatives = true;
    bool all_different = true;
    for (std::size_t i = 0; i < made.size(); ++i) {
        all_alternatives = all_alternatives && made[i].prefab == 2U;
        for (std::size_t j = i + 1; j < made.size(); ++j) {
            all_different = all_different && (made[i].x != made[j].x || made[i].y != made[j].y);
        }
    }
    CHECK(all_alternatives);
    CHECK(all_different); // five different cells
}

TEST_CASE("grid2d: the same seed chooses the same cells") {
    std::vector<float> first;
    for (int run = 0; run < 2; ++run) {
        Scene scene;
        grid2d::SpawnOnEmpty spawner;
        spawner.trigger = 4;
        spawner.count = 3;
        const auto entity = scene.app.world().spawn();
        CHECK(scene.app.world().insert(entity, spawner));
        std::vector<float> xs;
        scene.app.set_prefab_spawner([&xs](ge::app::App&, const ge::app::SpawnRequest& request) {
            xs.push_back(request.x * 1000.0F + request.y);
        });
        scene.app.world().resource<ge::app::ScriptedInput>()->add(0, "grow", 1.0F);
        scene.app.world().resource<ge::app::ScriptedInput>()->add(1, "grow", 0.0F);
        scene.app.run_frames(3);
        if (run == 0) {
            first = xs;
        } else {
            CHECK(xs == first);
        }
    }
}

TEST_CASE("grid2d: a Zone counts what sits on its cells and raises its action when more come") {
    Scene scene;
    const auto zone = scene.zone(0, 2, 4, 1);
    scene.app.run_frames(1);
    CHECK(scene.area(zone).count == 0U);
    CHECK(scene.area(zone).entered == 0U);
    CHECK(!scene.app.world().resource<ge::app::Actions>()->pressed("cell"));
    const auto first = scene.piece(1, 2);
    scene.piece(1, 1); // a row above: not in the zone
    scene.app.run_frames(1);
    CHECK(scene.area(zone).count == 1U);
    CHECK(scene.area(zone).entered == first.to_link());
    scene.app.run_frames(1);
    CHECK(scene.app.world().resource<ge::app::Actions>()->pressed("cell"));
    scene.app.run_frames(1);
    CHECK(!scene.app.world().resource<ge::app::Actions>()->pressed("cell"));
}

TEST_CASE(
    "grid2d: a Zone raises its action again for each entity that comes, not when one leaves") {
    Scene scene;
    const auto zone = scene.zone(0, 0, 2, 2);
    const auto first = scene.piece(0, 0);
    scene.app.run_frames(2);
    CHECK(scene.area(zone).count == 1U);
    const auto second = scene.piece(1, 1);
    scene.app.run_frames(1);
    CHECK(scene.area(zone).count == 2U);
    CHECK(scene.area(zone).entered == first.to_link()); // the oldest one
    scene.app.run_frames(1);
    CHECK(scene.app.world().resource<ge::app::Actions>()->pressed("cell"));
    scene.app.world().get<grid2d::GridPos>(first)->col = 3;
    scene.app.run_frames(1);
    CHECK(scene.area(zone).count == 1U);
    CHECK(scene.area(zone).entered == second.to_link());
    scene.app.run_frames(1);
    CHECK(!scene.app.world().resource<ge::app::Actions>()->pressed("cell"));
}

TEST_CASE("grid2d: a Zone with a field only counts the entities that have that number") {
    Scene scene;
    const auto zone = scene.zone(0, 0, 4, 3, true);
    scene.piece(0, 0, 0.0F);
    scene.app.run_frames(1);
    CHECK(scene.area(zone).count == 0U);
    const auto wanted = scene.piece(2, 1, 0.5F);
    scene.app.run_frames(1);
    CHECK(scene.area(zone).count == 1U);
    CHECK(scene.area(zone).entered == wanted.to_link());
}

TEST_CASE("grid2d: Disabled entities are not in a zone, and a Disabled zone does nothing") {
    Scene scene;
    const auto zone = scene.zone(0, 0, 4, 3);
    const auto hidden = scene.piece(1, 1);
    CHECK(scene.app.world().insert(hidden, transform2d::Disabled{}));
    scene.app.run_frames(1);
    CHECK(scene.area(zone).count == 0U);
    CHECK(scene.app.world().remove<transform2d::Disabled>(hidden));
    CHECK(scene.app.world().insert(zone, transform2d::Disabled{}));
    scene.app.run_frames(2);
    CHECK(scene.area(zone).count == 0U);
}

namespace {

ge::ecs::Entity add_key_cursor(Scene& scene, bool wrap) {
    const auto cursor = add_cursor(scene);
    auto* keys = scene.app.world().get<grid2d::GridCursor>(cursor);
    keys->left = 5;
    keys->right = 6;
    keys->up = 7;
    keys->down = 8;
    keys->accept = 9;
    keys->wrap = wrap;
    return cursor;
}

/// Presses a key on a tick and lets it go on the next.
void tap(Scene& scene, std::uint64_t tick, const char* action) {
    script(scene, tick, action, 1.0F);
    script(scene, tick + 1, action, 0.0F);
}

grid2d::GridCursor cursor_of(const Scene& scene, ge::ecs::Entity cursor) {
    const auto* now = scene.app.world().get<grid2d::GridCursor>(cursor);
    CHECK(now != nullptr);
    return now != nullptr ? *now : grid2d::GridCursor{};
}

} // namespace

namespace {

/// A second board, id 1: 3 cells of 10 with 4 between them, centred on (0, 0): centres at -14,
/// 0 and 14.
void add_spaced_row(Scene& scene) {
    const auto board = scene.app.world().spawn();
    CHECK(scene.app.world().insert(board, transform2d::Position{0.0F, 0.0F}));
    grid2d::Grid row;
    row.id = 1;
    row.columns = 3;
    row.rows = 1;
    row.cell_width = 10.0F;
    row.cell_height = 10.0F;
    row.spacing_x = 4.0F;
    CHECK(scene.app.world().insert(board, row));
}

} // namespace

TEST_CASE("grid2d: spacing puts space between the cells") {
    Scene scene;
    add_spaced_row(scene);
    const auto last = scene.piece(2, 0);
    scene.app.world().get<grid2d::GridPos>(last)->grid = 1;
    scene.app.run_frames(1);
    CHECK(scene.where(last).x == 14.0F);
    CHECK(scene.where(last).y == 0.0F);
}

TEST_CASE("grid2d: the space between two cells is on no cell") {
    Scene scene;
    add_spaced_row(scene);
    const auto layout = grid2d::layout_of(scene.app.world(), 1);
    REQUIRE(layout.has_value());
    CHECK(layout->col_at(-14.0F) == 0);
    CHECK(layout->col_at(-8.0F) == -1); // between the first and the second cell
    CHECK(layout->col_at(0.0F) == 1);
    CHECK(layout->col_at(18.0F) == 2);
}

TEST_CASE("grid2d: the pointer over a screen board is not seen through the camera") {
    Scene scene;
    const auto camera = scene.app.world().spawn();
    CHECK(scene.app.world().insert(camera, transform2d::Position{-500.0F, 0.0F}));
    CHECK(scene.app.world().insert(camera, camera2d::Camera{3.0F}));
    scene.app.world().each<grid2d::Grid>(
        [](ge::ecs::Entity, grid2d::Grid& grid) { grid.screen = true; });
    const auto cursor = add_cursor(scene);
    script(scene, 0, "pointer_x", 105.0F); // pixels of the window, the board's own units
    script(scene, 0, "pointer_y", 50.0F);
    scene.app.run_frames(2);
    CHECK(cursor_of(scene, cursor).over);
    CHECK(cursor_of(scene, cursor).col == 2);
    CHECK(cursor_of(scene, cursor).row == 1);
}

TEST_CASE("grid2d: keys move the cursor from cell to cell and stop at the edges") {
    Scene scene;
    const auto cursor = add_key_cursor(scene, false);
    tap(scene, 0, "right");
    tap(scene, 2, "down");
    scene.app.run_frames(4);
    CHECK(cursor_of(scene, cursor).over);
    CHECK(cursor_of(scene, cursor).col == 1);
    CHECK(cursor_of(scene, cursor).row == 1);
    tap(scene, 4, "left");
    tap(scene, 6, "left");
    tap(scene, 8, "up");
    tap(scene, 10, "up");
    scene.app.run_frames(8);
    CHECK(cursor_of(scene, cursor).col == 0); // stopped at the edge
    CHECK(cursor_of(scene, cursor).row == 0);
    tap(scene, 12, "accept");
    scene.app.run_frames(1);
    CHECK(cursor_of(scene, cursor).clicked);
    scene.app.run_frames(1);
    CHECK(scene.app.world().resource<ge::app::Actions>()->pressed("cell"));
}

TEST_CASE("grid2d: with wrap the keys go round, and a moving pointer takes the cursor back") {
    Scene scene;
    const auto cursor = add_key_cursor(scene, true);
    tap(scene, 0, "left");
    tap(scene, 2, "up");
    scene.app.run_frames(4);
    CHECK(cursor_of(scene, cursor).col == 3);
    CHECK(cursor_of(scene, cursor).row == 2);
    script(scene, 4, "pointer_x", 95.0F);
    script(scene, 4, "pointer_y", 40.0F);
    scene.app.run_frames(2);
    CHECK(!cursor_of(scene, cursor).keyed);
    CHECK(cursor_of(scene, cursor).col == 1);
    CHECK(cursor_of(scene, cursor).row == 0);
    // A key moves it again, and the pointer that does not move leaves it there.
    tap(scene, 6, "right");
    scene.app.run_frames(4);
    CHECK(cursor_of(scene, cursor).keyed);
    CHECK(cursor_of(scene, cursor).col == 2);
    CHECK(cursor_of(scene, cursor).row == 0);
}
