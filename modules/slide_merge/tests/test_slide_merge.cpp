#include "ge/app/app.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/mod/camera2d/camera2d.gen.hpp"
#include "ge/mod/grid2d/grid2d.gen.hpp"
#include "ge/mod/slide_merge/slide_merge.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

#include <cstdint>
#include <vector>

namespace {

namespace grid2d = ge::mod::grid2d;
namespace slide_merge = ge::mod::slide_merge;
namespace transform2d = ge::mod::transform2d;

// Actions: 0 left, 1 right, 2 up, 3 down, 4 moved, 5 stuck.
struct Scene {
    ge::app::App app;
    ge::ecs::Entity board;
    std::uint64_t tick = 0;

    explicit Scene(std::uint32_t mode = 0, std::int32_t columns = 4, std::int32_t rows = 4) {
        transform2d::install(app);
        camera2d_install();
        grid2d::install(app);
        slide_merge::install(app);
        CHECK(app.module_problems().empty());
        CHECK(app.schedule().problems().empty());
        app.world().insert_resource(
            ge::app::ActionNames{{"left", "right", "up", "down", "moved", "stuck"}});
        const auto frame = app.world().spawn();
        CHECK(app.world().insert(frame, transform2d::Position{0.0F, 0.0F}));
        grid2d::Grid grid;
        grid.columns = static_cast<std::uint32_t>(columns);
        grid.rows = static_cast<std::uint32_t>(rows);
        CHECK(app.world().insert(frame, grid));
        board = frame;
        slide_merge::SlideBoard rules;
        rules.left = 0;
        rules.right = 1;
        rules.up = 2;
        rules.down = 3;
        rules.moved_action = 4;
        rules.stuck_action = 5;
        rules.mode = mode;
        rules.vanish_ticks = 3;
        CHECK(app.world().insert(frame, rules));
    }

    void camera2d_install() { ge::mod::camera2d::install(app); }

    ge::ecs::Entity tile(std::int32_t col, std::int32_t row, std::int32_t value) {
        const auto entity = app.world().spawn();
        CHECK(app.world().insert(entity, transform2d::Position{0.0F, 0.0F}));
        grid2d::GridPos at;
        at.col = col;
        at.row = row;
        CHECK(app.world().insert(entity, at));
        slide_merge::Tile piece;
        piece.value = value;
        CHECK(app.world().insert(entity, piece));
        return entity;
    }

    ge::ecs::Entity obstacle(std::int32_t col, std::int32_t row) {
        const auto entity = app.world().spawn();
        CHECK(app.world().insert(entity, transform2d::Position{0.0F, 0.0F}));
        grid2d::GridPos at;
        at.col = col;
        at.row = row;
        CHECK(app.world().insert(entity, at));
        CHECK(app.world().insert(entity, grid2d::Blocked{}));
        return entity;
    }

    /// Presses an action for one tick, then lets the game run a few ticks more. Returns whether
    /// `watch` was raised during that time.
    bool press(const char* name, const char* watch = "moved") {
        auto* input = app.world().resource<ge::app::ScriptedInput>();
        input->add(tick, name, 1.0F);
        input->add(tick + 1, name, 0.0F);
        bool seen = false;
        for (int i = 0; i < 2; ++i) {
            app.run_frames(1);
            ++tick;
            seen = seen || app.world().resource<ge::app::Actions>()->pressed(watch);
        }
        return seen;
    }

    void settle() {
        app.run_frames(6);
        tick += 6;
    }

    /// The value of the tile on a cell (not counting those being merged away), 0 for none.
    [[nodiscard]] std::int32_t at(std::int32_t col, std::int32_t row) {
        std::int32_t value = 0;
        app.world().each<const slide_merge::Tile, const grid2d::GridPos>(
            [&](ge::ecs::Entity, const slide_merge::Tile& tile, const grid2d::GridPos& pos) {
                if (!tile.dying && pos.col == col && pos.row == row) {
                    value = tile.value;
                }
            });
        return value;
    }

    [[nodiscard]] std::vector<std::int32_t> row(std::int32_t index) {
        return {at(0, index), at(1, index), at(2, index), at(3, index)};
    }

    [[nodiscard]] std::vector<std::int32_t> column(std::int32_t index) {
        return {at(index, 0), at(index, 1), at(index, 2), at(index, 3)};
    }

    [[nodiscard]] const slide_merge::SlideBoard& rules() const {
        return *app.world().get<slide_merge::SlideBoard>(board);
    }
};

using Values = std::vector<std::int32_t>;

} // namespace

TEST_CASE("slide_merge: tiles slide as far as they can, and two equal ones make their sum") {
    Scene scene;
    scene.tile(1, 0, 2);
    scene.tile(2, 0, 2);
    scene.tile(3, 0, 4);
    CHECK(scene.press("left"));
    CHECK((scene.row(0) == Values{4, 4, 0, 0}));
    CHECK(scene.rules().score == 4);
    CHECK(scene.rules().moves == 1);
}

TEST_CASE("slide_merge: a tile merges once per move, starting from the edge it goes to") {
    Scene scene;
    for (std::int32_t col = 0; col < 4; ++col) {
        scene.tile(col, 0, 2);
    }
    CHECK(scene.press("right"));
    CHECK((scene.row(0) == Values{0, 0, 4, 4}));
    CHECK(scene.rules().score == 8);
    Scene three;
    three.tile(0, 0, 2);
    three.tile(1, 0, 2);
    three.tile(2, 0, 2);
    CHECK(three.press("left"));
    CHECK((three.row(0) == Values{4, 2, 0, 0})); // the two nearest the edge merge
}

TEST_CASE("slide_merge: up and down slide along the columns") {
    Scene scene;
    scene.tile(2, 1, 8);
    scene.tile(2, 3, 8);
    CHECK(scene.press("up"));
    CHECK((scene.column(2) == Values{16, 0, 0, 0}));
    scene.tile(2, 2, 4);
    CHECK(scene.press("down"));
    CHECK((scene.column(2) == Values{0, 0, 16, 4}));
}

TEST_CASE("slide_merge: a move that changes nothing is not a move") {
    Scene scene;
    scene.tile(0, 0, 2);
    scene.tile(1, 0, 4);
    scene.tile(2, 0, 8);
    CHECK(!scene.press("left"));
    CHECK(scene.rules().moves == 0);
    CHECK(scene.rules().score == 0);
    CHECK((scene.row(0) == Values{2, 4, 8, 0}));
}

TEST_CASE("slide_merge: an obstacle stops the tiles and the merging") {
    Scene scene;
    scene.tile(0, 0, 2);
    scene.obstacle(1, 0);
    scene.tile(2, 0, 2);
    scene.tile(3, 0, 2);
    CHECK(scene.press("left"));
    CHECK((scene.row(0) == Values{2, 0, 4, 0}));
}

TEST_CASE("slide_merge: in next mode two equal tiles make the next value (levels, letters)") {
    Scene scene(1);
    scene.tile(0, 0, 1);
    scene.tile(1, 0, 1);
    scene.tile(2, 0, 2);
    CHECK(scene.press("left"));
    CHECK((scene.row(0) == Values{2, 2, 0, 0}));
    CHECK(scene.rules().highest == 2);
}

TEST_CASE("slide_merge: in none mode the tiles only slide") {
    Scene scene(2);
    scene.tile(0, 0, 2);
    scene.tile(1, 0, 2);
    CHECK(scene.press("right"));
    CHECK((scene.row(0) == Values{0, 0, 2, 2}));
    CHECK(scene.rules().score == 0);
}

TEST_CASE("slide_merge: the tile that joins another vanishes, the one it joins pops") {
    Scene scene;
    const auto first = scene.tile(0, 0, 2);
    const auto second = scene.tile(1, 0, 2);
    CHECK(scene.press("left"));
    CHECK(scene.app.world().get<slide_merge::Tile>(first)->value == 4);
    CHECK(scene.app.world().get<slide_merge::Tile>(second)->dying);
    CHECK(scene.app.world().has<transform2d::Disabled>(second));
    CHECK(scene.app.world().get<transform2d::Scale>(first)->factor == 1.25F);
    scene.settle();
    CHECK(!scene.app.world().is_alive(second));
    CHECK(scene.app.world().is_alive(first));
}

TEST_CASE("slide_merge: the highest value of the board is known") {
    Scene scene;
    scene.tile(0, 0, 8);
    scene.tile(3, 3, 32);
    scene.settle();
    CHECK(scene.rules().highest == 32);
}

TEST_CASE("slide_merge: a full board where nothing can merge is stuck, and says so once") {
    Scene scene(0, 2, 2);
    scene.tile(0, 0, 2);
    scene.tile(1, 0, 4);
    scene.tile(0, 1, 4);
    scene.tile(1, 1, 2);
    CHECK(!scene.rules().no_moves);
    scene.app.run_frames(1);
    CHECK(scene.rules().no_moves);
    scene.app.run_frames(1);
    CHECK(scene.app.world().resource<ge::app::Actions>()->pressed("stuck"));
    scene.app.run_frames(1);
    CHECK(!scene.app.world().resource<ge::app::Actions>()->pressed("stuck"));
}

TEST_CASE("slide_merge: a board that can still merge is not stuck") {
    Scene scene(0, 2, 2);
    scene.tile(0, 0, 2);
    scene.tile(1, 0, 2);
    scene.tile(0, 1, 4);
    scene.tile(1, 1, 8);
    scene.settle();
    CHECK(!scene.rules().no_moves);
}

TEST_CASE("slide_merge: a Disabled board ignores its actions") {
    Scene scene;
    scene.tile(3, 0, 2);
    CHECK(scene.app.world().insert(scene.board, transform2d::Disabled{}));
    CHECK(!scene.press("left"));
    CHECK((scene.row(0) == Values{0, 0, 0, 2}));
}
