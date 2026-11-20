#include "ge/app/app.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/mod/camera2d/camera2d.gen.hpp"
#include "ge/mod/grid2d/grid2d.gen.hpp"
#include "ge/mod/grid_moves/board.hpp"
#include "ge/mod/grid_moves/grid_moves.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"
#include "ge/mod/turns/turns.gen.hpp"

#include "mini_test.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

namespace grid2d = ge::mod::grid2d;
namespace gm = ge::mod::grid_moves;
namespace transform2d = ge::mod::transform2d;
namespace turns = ge::mod::turns;

// None of these pieces is from a game of chess: kinds 0 to 3 are a "rook" that slides along
// the lines, a "king" that steps to any neighbour, a "pawn" that goes down the board, and a
// "camel" that jumps by (3, 1). The board is five by five, the cells 10 by 10 units.
constexpr std::uint32_t rook = 0;
constexpr std::uint32_t king = 1;
constexpr std::uint32_t pawn = 2;
constexpr std::uint32_t camel = 3;

using Cells = std::vector<int>; // col * 10 + row, sorted

// Actions: 0 moved, 1 refused, 2 end_turn, 3 pointer_x, 4 pointer_y, 5 click.
struct Scene {
    ge::app::App app;
    ge::ecs::Entity request;
    ge::ecs::Entity keeper;

    explicit Scene(std::uint32_t columns = 5, std::uint32_t rows = 5) {
        transform2d::install(app);
        ge::mod::camera2d::install(app);
        grid2d::install(app);
        turns::install(app);
        gm::install(app);
        CHECK(app.module_problems().empty());
        CHECK(app.schedule().problems().empty());
        app.world().insert_resource(ge::app::ActionNames{
            {"moved", "refused", "end_turn", "pointer_x", "pointer_y", "click"}});
        const auto board = app.world().spawn();
        CHECK(app.world().insert(board, transform2d::Position{0.0F, 0.0F}));
        grid2d::Grid grid;
        grid.columns = columns;
        grid.rows = rows;
        grid.cell_width = 10.0F;
        grid.cell_height = 10.0F;
        CHECK(app.world().insert(board, grid));
        keeper = app.world().spawn();
        turns::Turn turn;
        turn.end_action = 2;
        CHECK(app.world().insert(keeper, turn));
        request = app.world().spawn();
        gm::MoveRequest ask;
        ask.moved_action = 0;
        ask.refused_action = 1;
        ask.ends_turn = false; // most tests move the same side several times
        CHECK(app.world().insert(request, ask));
        steps();
        sliders();
    }

    void add(const gm::Pattern& pattern) {
        const auto entity = app.world().spawn();
        CHECK(app.world().insert(entity, pattern));
    }

    static gm::Pattern slide(std::uint32_t kind, std::int32_t dx, std::int32_t dy,
                             std::uint32_t range = 0) {
        gm::Pattern pattern;
        pattern.kind = kind;
        pattern.dx = dx;
        pattern.dy = dy;
        pattern.range = range;
        return pattern;
    }

    void sliders() {
        add(slide(rook, 1, 0));
        add(slide(rook, -1, 0));
        add(slide(rook, 0, 1));
        add(slide(rook, 0, -1));
    }

    void steps() {
        for (std::int32_t dx = -1; dx <= 1; ++dx) {
            for (std::int32_t dy = -1; dy <= 1; ++dy) {
                if (dx != 0 || dy != 0) {
                    add(slide(king, dx, dy, 1));
                }
            }
        }
    }

    ge::ecs::Entity piece(std::uint32_t kind, std::uint32_t player, std::int32_t col,
                          std::int32_t row) {
        const auto entity = app.world().spawn();
        CHECK(app.world().insert(entity, transform2d::Position{0.0F, 0.0F}));
        grid2d::GridPos at;
        at.col = col;
        at.row = row;
        CHECK(app.world().insert(entity, at));
        CHECK(app.world().insert(entity, turns::Team{player}));
        gm::Mover mover;
        mover.kind = kind;
        CHECK(app.world().insert(entity, mover));
        return entity;
    }

    ge::ecs::Entity guard(std::uint32_t kind, std::uint32_t player, std::int32_t col,
                          std::int32_t row) {
        const auto entity = piece(kind, player, col, row);
        CHECK(app.world().insert(entity, gm::Guard{}));
        return entity;
    }

    ge::ecs::Entity wall(std::int32_t col, std::int32_t row) {
        const auto entity = app.world().spawn();
        grid2d::GridPos at;
        at.col = col;
        at.row = row;
        CHECK(app.world().insert(entity, at));
        CHECK(app.world().insert(entity, grid2d::Blocked{}));
        return entity;
    }

    [[nodiscard]] gm::TurnInfo turn() { return gm::read_turn(app.world()); }

    /// The cells a piece may legally go to, sorted.
    [[nodiscard]] Cells moves_of(ge::ecs::Entity entity) {
        const auto board = gm::read_board(app.world(), 0, turn().number);
        Cells cells;
        if (!board) {
            return cells;
        }
        const gm::Patterns patterns = gm::read_patterns(app.world());
        for (std::size_t i = 0; i < board->pieces.size(); ++i) {
            if (board->pieces[i].entity == entity) {
                for (const gm::Move& move : gm::legal_moves(*board, patterns, i)) {
                    cells.push_back(move.col * 10 + move.row);
                }
            }
        }
        std::ranges::sort(cells);
        return cells;
    }

    [[nodiscard]] bool can_go(ge::ecs::Entity entity, std::int32_t col, std::int32_t row) {
        const Cells cells = moves_of(entity);
        return std::ranges::find(cells, col * 10 + row) != cells.end();
    }

    /// Asks for a move and lets the game play it.
    void ask(std::int32_t from_col, std::int32_t from_row, std::int32_t to_col,
             std::int32_t to_row) {
        auto* move = app.world().get<gm::MoveRequest>(request);
        move->from_col = from_col;
        move->from_row = from_row;
        move->to_col = to_col;
        move->to_row = to_row;
        move->pending = true;
        app.run_frames(2);
    }

    [[nodiscard]] const gm::MoveRequest& requests() const {
        return *app.world().get<gm::MoveRequest>(request);
    }
    [[nodiscard]] grid2d::GridPos where(ge::ecs::Entity entity) const {
        return *app.world().get<grid2d::GridPos>(entity);
    }
    [[nodiscard]] const gm::Mover& mover(ge::ecs::Entity entity) const {
        return *app.world().get<gm::Mover>(entity);
    }
};

} // namespace

TEST_CASE(
    "grid_moves: a slider goes as far as it can, stops before its own side and on a capture") {
    Scene scene;
    const auto slider = scene.piece(rook, 0, 2, 2);
    scene.piece(rook, 0, 2, 0); // its own: it can reach (2, 1) and no further
    scene.piece(rook, 1, 4, 2); // another side: it can be captured, not passed
    scene.wall(0, 2);           // a wall: nothing beyond it
    CHECK((scene.moves_of(slider) == Cells{12, 21, 23, 24, 32, 42}));
}

TEST_CASE("grid_moves: the range limits a slider, and a jumper goes over everything") {
    Scene scene;
    scene.add(Scene::slide(pawn, 0, 1, 2)); // two steps down
    const auto walker = scene.piece(pawn, 0, 1, 0);
    CHECK((scene.moves_of(walker) == Cells{11, 12}));
    scene.piece(rook, 1, 1, 1);
    CHECK((scene.moves_of(walker) == Cells{11})); // the capture is a move too
    gm::Pattern leap = Scene::slide(camel, 3, 1);
    leap.jump = true;
    scene.add(leap);
    leap.dx = 1;
    leap.dy = 3;
    scene.add(leap);
    scene.piece(rook, 1, 1, 0);
    scene.piece(rook, 1, 2, 0);
    scene.piece(rook, 1, 3, 0);
    const auto jumper = scene.piece(camel, 0, 0, 0);
    CHECK((scene.moves_of(jumper) == Cells{13, 31}));
}

TEST_CASE("grid_moves: a pattern can be only for a move, or only for a capture") {
    Scene scene;
    gm::Pattern forward = Scene::slide(pawn, 0, 1, 1);
    forward.mode = gm::mode_move_only;
    scene.add(forward);
    gm::Pattern diagonal = Scene::slide(pawn, 1, 1, 1);
    diagonal.mode = gm::mode_capture_only;
    scene.add(diagonal);
    const auto walker = scene.piece(pawn, 0, 1, 1);
    CHECK((scene.moves_of(walker) == Cells{12}));
    scene.piece(rook, 1, 1, 2); // in front: blocks
    CHECK(scene.moves_of(walker).empty());
    scene.piece(rook, 1, 2, 2); // on the diagonal: can be taken
    CHECK((scene.moves_of(walker) == Cells{22}));
}

TEST_CASE("grid_moves: a double step is for a first move only, and needs the cells free") {
    Scene scene;
    gm::Pattern one = Scene::slide(pawn, 0, 1, 1);
    one.mode = gm::mode_move_only;
    scene.add(one);
    gm::Pattern two = Scene::slide(pawn, 0, 1, 2);
    two.mode = gm::mode_move_only;
    two.min_range = 2;
    two.first_move_only = true;
    scene.add(two);
    const auto walker = scene.piece(pawn, 0, 1, 0);
    CHECK((scene.moves_of(walker) == Cells{11, 12}));
    const auto blocker = scene.piece(rook, 1, 1, 1);
    CHECK(scene.moves_of(walker).empty());
    CHECK(scene.app.world().despawn(blocker));
    scene.ask(1, 0, 1, 1);
    CHECK(scene.mover(walker).moves_made == 1);
    CHECK((scene.moves_of(walker) == Cells{12})); // no double step any more
}

TEST_CASE("grid_moves: a move that leaves a guard threatened is not legal") {
    Scene scene;
    const auto leader = scene.guard(king, 0, 0, 0);
    const auto shield = scene.piece(rook, 0, 1, 0);
    scene.piece(rook, 1, 4, 0);
    scene.piece(rook, 1, 2, 4); // threatens the column 2
    // The shield is pinned on the row: it may only slide along it.
    CHECK((scene.moves_of(shield) == Cells{20, 30, 40}));
    // The leader may not step onto a threatened cell: not (1, 1) (nothing threatens it), but
    // (0, 1) is free too; the row 0 is shielded, so (1, 0) is held by its own piece.
    CHECK((scene.moves_of(leader) == Cells{1, 11}));
}

TEST_CASE("grid_moves: the status of a side says threatened, no move left, or both") {
    Scene scene;
    const auto side = scene.app.world().spawn();
    CHECK(scene.app.world().insert(side, gm::Side{0, 0, 0}));
    scene.guard(king, 0, 0, 0);
    scene.app.run_frames(2);
    CHECK(scene.app.world().get<gm::Side>(side)->status == 0);
    // Threatened along the row, but able to step out of it.
    scene.piece(rook, 1, 4, 0);
    scene.app.run_frames(2);
    CHECK(scene.app.world().get<gm::Side>(side)->status == 1);
    // A second piece takes the other row too: nowhere to go: mate.
    scene.piece(rook, 1, 4, 1);
    scene.app.run_frames(2);
    CHECK(scene.app.world().get<gm::Side>(side)->status == 3);
}

TEST_CASE("grid_moves: a side that cannot move and is not threatened is stuck") {
    Scene scene;
    const auto side = scene.app.world().spawn();
    CHECK(scene.app.world().insert(side, gm::Side{0, 0, 0}));
    scene.guard(king, 0, 0, 0);
    scene.piece(rook, 1, 4, 1); // takes the row 1
    scene.piece(rook, 1, 1, 4); // takes the column 1
    scene.app.run_frames(2);
    CHECK(scene.app.world().get<gm::Side>(side)->status == 2);
}

TEST_CASE("grid_moves: a companion goes along with the move, and only when it may") {
    Scene scene;
    gm::Pattern pair = Scene::slide(king, 1, 0, 2);
    pair.min_range = 2;
    pair.mode = gm::mode_move_only;
    pair.first_move_only = true;
    pair.safe_path = true;
    pair.companion_kind = rook;
    pair.companion_from = 3;
    pair.companion_to = 1;
    scene.add(pair);
    const auto leader = scene.guard(king, 0, 0, 0);
    const auto partner = scene.piece(rook, 0, 3, 0);
    CHECK(scene.can_go(leader, 2, 0));
    // Something between them: not possible.
    const auto between = scene.piece(rook, 0, 1, 0);
    CHECK(!scene.can_go(leader, 2, 0));
    CHECK(scene.app.world().despawn(between));
    // A cell crossed is threatened: not possible.
    const auto watcher = scene.piece(rook, 1, 1, 4);
    CHECK(!scene.can_go(leader, 2, 0));
    CHECK(scene.app.world().despawn(watcher));
    scene.ask(0, 0, 2, 0);
    CHECK(scene.where(leader).col == 2);
    CHECK(scene.where(partner).col == 1);
    CHECK(scene.mover(partner).moves_made == 1);
}

TEST_CASE("grid_moves: a companion that has already moved does not come") {
    Scene scene;
    gm::Pattern pair = Scene::slide(king, 1, 0, 2);
    pair.min_range = 2;
    pair.mode = gm::mode_move_only;
    pair.companion_kind = rook;
    pair.companion_from = 3;
    pair.companion_to = 1;
    scene.add(pair);
    const auto leader = scene.guard(king, 0, 0, 0);
    const auto partner = scene.piece(rook, 0, 3, 0);
    CHECK(scene.app.world().get<gm::Mover>(partner) != nullptr);
    scene.app.world().get<gm::Mover>(partner)->moves_made = 1;
    CHECK(!scene.can_go(leader, 2, 0));
}

TEST_CASE("grid_moves: a victim elsewhere than on the destination is taken, if it has just moved") {
    Scene scene;
    gm::Pattern aside = Scene::slide(pawn, 1, 1, 1);
    aside.mode = gm::mode_move_only;
    aside.victim_kind = pawn;
    aside.victim_dx = 0;
    aside.victim_dy = -1;
    aside.victim_min_distance = 2;
    scene.add(aside);
    const auto hunter = scene.piece(pawn, 0, 1, 2);
    const auto prey = scene.piece(pawn, 1, 2, 2);
    CHECK(!scene.can_go(hunter, 2, 3)); // it has not moved
    auto* prey_mover = scene.app.world().get<gm::Mover>(prey);
    prey_mover->last_move_turn = 0;
    prey_mover->last_move_distance = 2;
    CHECK(!scene.can_go(hunter, 2, 3)); // it moved in this very turn, not in the one before
    scene.app.world().get<turns::Turn>(scene.keeper)->number = 1;
    CHECK(scene.can_go(hunter, 2, 3));
    prey_mover->last_move_distance = 1; // a short step
    CHECK(!scene.can_go(hunter, 2, 3));
    prey_mover->last_move_distance = 2;
    scene.app.world().get<turns::Turn>(scene.keeper)->number = 2; // too late
    CHECK(!scene.can_go(hunter, 2, 3));
    scene.app.world().get<turns::Turn>(scene.keeper)->number = 1;
    scene.ask(1, 2, 2, 3);
    CHECK(scene.where(hunter).col == 2);
    CHECK(scene.where(hunter).row == 3);
    CHECK(!scene.app.world().is_alive(prey));
}

TEST_CASE("grid_moves: a move request that is not legal is refused and counted") {
    Scene scene;
    scene.piece(rook, 0, 0, 0);
    scene.piece(rook, 1, 4, 4);
    scene.piece(rook, 1, 0, 3);
    scene.ask(0, 0, 3, 3); // not a line
    CHECK(scene.requests().refused == 1);
    scene.ask(4, 4, 4, 0); // the other player's piece: not their turn
    CHECK(scene.requests().refused == 2);
    scene.ask(2, 2, 2, 3); // no piece there
    CHECK(scene.requests().refused == 3);
    CHECK(scene.requests().moves == 0);
}

TEST_CASE("grid_moves: a move request that is legal is played, and the piece remembers it") {
    Scene scene;
    const auto mine = scene.piece(rook, 0, 0, 0);
    const auto theirs = scene.piece(rook, 1, 4, 4);
    const auto prey = scene.piece(rook, 1, 0, 3);
    scene.ask(0, 0, 0, 3); // a capture
    CHECK(scene.requests().moves == 1);
    CHECK(scene.where(mine).row == 3);
    CHECK(!scene.app.world().is_alive(prey));
    CHECK(scene.app.world().is_alive(theirs));
    CHECK(scene.mover(mine).moves_made == 1);
    CHECK(scene.mover(mine).last_move_distance == 3);
}

TEST_CASE("grid_moves: the last move is written on the request and on the piece") {
    Scene scene;
    const auto mine = scene.piece(rook, 0, 0, 0);
    scene.ask(0, 0, 0, 3);
    CHECK(scene.mover(mine).last_move_turn == 0);
    CHECK(scene.requests().has_last);
    CHECK(scene.requests().last_to_row == 3);
}

TEST_CASE("grid_moves: a move passes the turn to the next player, who alone may then move") {
    Scene scene;
    scene.app.world().get<gm::MoveRequest>(scene.request)->ends_turn = true;
    const auto first = scene.piece(rook, 0, 0, 0);
    const auto second = scene.piece(rook, 1, 4, 4);
    scene.ask(0, 0, 0, 2);
    CHECK(scene.turn().current == 1);
    CHECK(scene.turn().number == 1);
    scene.ask(0, 2, 0, 3); // the first player again: not their turn
    CHECK(scene.requests().refused == 1);
    scene.ask(4, 4, 4, 2);
    CHECK(scene.where(second).row == 2);
    CHECK(scene.mover(second).last_move_turn == 1);
    CHECK(scene.where(first).row == 2);
    CHECK(scene.turn().current == 0);
    CHECK(scene.turn().number == 2);
}

TEST_CASE("grid_moves: only the side to move can be stuck") {
    Scene scene;
    scene.app.world().get<gm::MoveRequest>(scene.request)->ends_turn = true;
    const auto white = scene.app.world().spawn();
    CHECK(scene.app.world().insert(white, gm::Side{0, 0, 0}));
    const auto black = scene.app.world().spawn();
    CHECK(scene.app.world().insert(black, gm::Side{0, 1, 0}));
    scene.guard(king, 0, 0, 0);
    scene.guard(king, 1, 4, 4);
    scene.piece(rook, 1, 4, 1); // takes the row 1: the white leader has no move
    scene.piece(rook, 1, 1, 4); // and the column 1
    scene.app.run_frames(2);
    CHECK(scene.app.world().get<gm::Side>(white)->status == 2); // its turn: stuck
    CHECK(scene.app.world().get<gm::Side>(black)->status == 0);
    scene.app.world().get<turns::Turn>(scene.keeper)->current = 1;
    scene.app.run_frames(2);
    CHECK(scene.app.world().get<gm::Side>(white)->status == 0); // not its turn
}

TEST_CASE("grid_moves: pieces of any side move when the game has no turn") {
    ge::app::App app;
    transform2d::install(app);
    ge::mod::camera2d::install(app);
    grid2d::install(app);
    turns::install(app);
    gm::install(app);
    const auto board = app.world().spawn();
    CHECK(app.world().insert(board, transform2d::Position{0.0F, 0.0F}));
    CHECK(app.world().insert(board, grid2d::Grid{}));
    const auto entity = app.world().spawn();
    CHECK(app.world().insert(entity, grid2d::GridPos{}));
    CHECK(app.world().insert(entity, turns::Team{1}));
    CHECK(app.world().insert(entity, gm::Mover{}));
    const auto pattern = app.world().spawn();
    CHECK(app.world().insert(pattern, Scene::slide(0, 1, 0, 1)));
    const auto board_view = gm::read_board(app.world(), 0, 0);
    REQUIRE(board_view.has_value());
    const gm::Patterns patterns = gm::read_patterns(app.world());
    CHECK(gm::legal_moves(*board_view, patterns, 0).size() == 1);
}

TEST_CASE("grid_moves: an invented piece that slides like a queen and jumps like a knight") {
    Scene scene;
    constexpr std::uint32_t amazon = 5;
    for (std::int32_t dx = -1; dx <= 1; ++dx) {
        for (std::int32_t dy = -1; dy <= 1; ++dy) {
            if (dx != 0 || dy != 0) {
                scene.add(Scene::slide(amazon, dx, dy));
            }
        }
    }
    for (const auto& [dx, dy] : std::vector<std::pair<std::int32_t, std::int32_t>>{
             {1, 2}, {2, 1}, {-1, 2}, {-2, 1}, {1, -2}, {2, -1}, {-1, -2}, {-2, -1}}) {
        gm::Pattern leap = Scene::slide(amazon, dx, dy);
        leap.jump = true;
        scene.add(leap);
    }
    const auto piece = scene.piece(amazon, 0, 2, 2);
    CHECK(scene.moves_of(piece).size() == 24); // 8 lines of 2 cells, 8 jumps
    scene.piece(rook, 0, 3, 3);                // its own piece blocks a diagonal, not a jump
    CHECK(scene.moves_of(piece).size() == 22);
}

TEST_CASE("grid_moves: a unit with a short reach shows the cells it can reach") {
    Scene scene;
    constexpr std::uint32_t scout = 4;
    scene.add(Scene::slide(scout, 1, 0, 2));
    scene.add(Scene::slide(scout, -1, 0, 2));
    scene.add(Scene::slide(scout, 0, 1, 2));
    scene.add(Scene::slide(scout, 0, -1, 2));
    const auto unit = scene.piece(scout, 0, 2, 2);
    scene.wall(2, 3);
    // Two cells along the row and the column, but not through the wall below it.
    CHECK((scene.moves_of(unit) == Cells{2, 12, 20, 21, 32, 42}));
}

namespace {

/// A board with a rook of each side, a picker and the markers that show its moves.
struct Touching {
    Scene scene;
    ge::ecs::Entity mine;
    ge::ecs::Entity theirs;
    ge::ecs::Entity picker;
    ge::ecs::Entity quiet;
    ge::ecs::Entity taking;
    ge::ecs::Entity selected;
    std::uint64_t tick = 2; // the constructor has played two ticks

    Touching() {
        mine = scene.piece(rook, 0, 0, 0);
        theirs = scene.piece(rook, 1, 0, 3);
        const auto cursor = scene.app.world().spawn();
        grid2d::GridCursor look;
        look.pointer_x = 3;
        look.pointer_y = 4;
        look.click = 5;
        CHECK(scene.app.world().insert(cursor, look));
        picker = scene.app.world().spawn();
        CHECK(scene.app.world().insert(picker, gm::Picker{}));
        quiet = marker(0, 0, 2);
        taking = marker(1, 0, 3);
        selected = marker(2, 4, 4);
        scene.app.world().get<turns::Turn>(scene.keeper)->end_action = 0;
        scene.app.run_frames(2);
    }

    ge::ecs::Entity marker(std::uint32_t what, std::int32_t col, std::int32_t row) {
        const auto entity = scene.app.world().spawn();
        grid2d::GridPos at;
        at.col = col;
        at.row = row;
        CHECK(scene.app.world().insert(entity, at));
        CHECK(scene.app.world().insert(entity, transform2d::Position{0.0F, 0.0F}));
        gm::Marker mark;
        mark.what = what;
        CHECK(scene.app.world().insert(entity, mark));
        return entity;
    }

    /// A touch on a cell, and a few ticks for the game to answer.
    void touch(std::int32_t col, std::int32_t row) {
        auto* input = scene.app.world().resource<ge::app::ScriptedInput>();
        input->add(tick, "pointer_x", static_cast<float>(col - 2) * 10.0F);
        input->add(tick, "pointer_y", static_cast<float>(row - 2) * 10.0F);
        input->add(tick + 1, "click", 1.0F);
        input->add(tick + 2, "click", 0.0F);
        scene.app.run_frames(5);
        tick += 5;
    }

    [[nodiscard]] bool shown(ge::ecs::Entity entity) const {
        return !scene.app.world().has<transform2d::Hidden>(entity);
    }
    [[nodiscard]] const gm::Picker& choice() const {
        return *scene.app.world().get<gm::Picker>(picker);
    }
};

} // namespace

TEST_CASE("grid_moves: a touch selects a mover of the player to move") {
    Touching game;
    CHECK(!game.choice().has);
    game.touch(0, 0);
    CHECK(game.choice().has);
    CHECK(game.choice().col == 0);
    CHECK(game.choice().row == 0);
}

TEST_CASE("grid_moves: the selection shows where the piece can go") {
    Touching game;
    CHECK(!game.shown(game.quiet));
    game.touch(0, 0);
    CHECK(game.shown(game.quiet));    // (0, 2): free, on the way
    CHECK(game.shown(game.taking));   // (0, 3): the other side's piece
    CHECK(game.shown(game.selected)); // follows the selection
    CHECK(game.scene.where(game.selected).col == 0);
    CHECK(game.scene.where(game.selected).row == 0);
}

TEST_CASE("grid_moves: a second touch on a cell it can reach plays the move") {
    Touching game;
    game.touch(0, 0);
    game.touch(0, 2);
    CHECK(game.scene.where(game.mine).row == 2);
    CHECK(game.scene.requests().moves == 1);
    CHECK(!game.choice().has);
    CHECK(!game.shown(game.quiet));
    CHECK(!game.shown(game.selected));
}

TEST_CASE("grid_moves: the other side's pieces are not selected, and a touch elsewhere lets go") {
    Touching game;
    game.touch(0, 3);
    CHECK(!game.choice().has);
    game.touch(0, 0);
    CHECK(game.choice().has);
    game.touch(3, 1); // not a cell it can reach, and no piece there
    CHECK(!game.choice().has);
    CHECK(game.scene.requests().moves == 0);
    CHECK(game.scene.requests().refused == 0);
}

TEST_CASE("grid_moves: a touch on another mover of the same side changes the selection") {
    Touching game;
    const auto other = game.scene.piece(rook, 0, 4, 0);
    game.touch(0, 0);
    game.touch(4, 0);
    CHECK(game.choice().has);
    CHECK(game.choice().col == 4);
    CHECK(game.choice().selected == other.to_link());
}

TEST_CASE("grid_moves: the last move is marked on both of its cells") {
    Touching game;
    const auto last_from = game.marker(3, 4, 4);
    const auto last_to = game.marker(4, 4, 4);
    game.scene.app.run_frames(2);
    game.tick += 2;
    CHECK(!game.shown(last_from));
    game.touch(0, 0);
    game.touch(4, 0);
    CHECK(game.shown(last_from));
    CHECK(game.scene.where(last_from).col == 0);
    CHECK(game.scene.where(last_from).row == 0);
    CHECK(game.shown(last_to));
    CHECK(game.scene.where(last_to).col == 4);
    CHECK(game.scene.where(last_to).row == 0);
}

TEST_CASE("grid_moves: a threatened guard is marked") {
    Touching game;
    const auto danger = game.marker(5, 4, 4);
    game.scene.guard(king, 1, 4, 3);
    game.scene.app.run_frames(2);
    game.tick += 2;
    CHECK(!game.shown(danger));
    game.touch(0, 0);
    game.touch(4, 0); // the rook now looks down the column 4
    CHECK(game.shown(danger));
    CHECK(game.scene.where(danger).col == 4);
    CHECK(game.scene.where(danger).row == 3);
}
