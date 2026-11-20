#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/ecs/commands.hpp"
#include "ge/mod/grid2d/grid2d.gen.hpp"
#include "ge/mod/grid_moves/board.hpp"
#include "ge/mod/grid_moves/grid_moves.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"
#include "ge/mod/turns/turns.gen.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ge::mod::grid_moves {

namespace {

void raise(ecs::World& world, std::uint32_t action) {
    const auto* names = world.resource<app::ActionNames>();
    auto* actions = world.resource<app::Actions>();
    if (names != nullptr && actions != nullptr && action < names->names.size()) {
        actions->raise(names->names[action]);
    }
}

/// Passes the turn to the next player, as turns.Turn does for its end action.
void end_turn(ecs::World& world) {
    bool done = false;
    world.each<turns::Turn>([&](ecs::Entity, turns::Turn& turn) {
        if (done) {
            return;
        }
        done = true;
        turn.current = turn.players == 0U ? 0U : (turn.current + 1U) % turn.players;
        ++turn.number;
        raise(world, turn.changed_action);
    });
}

/// The cell as a pair, for comparing.
bool is_cell(const Move& move, std::int32_t col, std::int32_t row) {
    return move.col == col && move.row == row;
}

std::optional<Move> move_to(const std::vector<Move>& moves, std::int32_t col, std::int32_t row) {
    for (const Move& move : moves) {
        if (is_cell(move, col, row)) {
            return move;
        }
    }
    return std::nullopt;
}

/// Writes what a move did to the board back to the entities.
void write_back(ecs::World& world, const Board& before, const Board& after) {
    auto* commands = world.resource<ecs::Commands>();
    for (std::size_t i = 0; i < before.pieces.size(); ++i) {
        const Piece& was = before.pieces[i];
        const Piece& now = after.pieces[i];
        if (was.alive && !now.alive) {
            commands->despawn(now.entity);
            continue;
        }
        auto* at = world.get<grid2d::GridPos>(now.entity);
        auto* mover = world.get<Mover>(now.entity);
        if (at == nullptr || mover == nullptr) {
            continue;
        }
        at->col = now.col;
        at->row = now.row;
        mover->moves_made = now.moves_made;
        mover->last_move_turn = now.last_turn;
        mover->last_move_distance = now.last_distance;
    }
}

/// Plays a request if it is legal for the player whose turn it is.
bool try_play(ecs::World& world, MoveRequest& request, const Patterns& patterns,
              const TurnInfo& turn) {
    const auto board = read_board(world, request.grid, turn.number);
    if (!board || !board->inside(request.from_col, request.from_row) ||
        !board->inside(request.to_col, request.to_row)) {
        return false;
    }
    const std::ptrdiff_t found = board->at(request.from_col, request.from_row);
    if (found < 0 ||
        (turn.present && board->pieces[static_cast<std::size_t>(found)].team != turn.current)) {
        return false;
    }
    const auto index = static_cast<std::size_t>(found);
    const auto move = move_to(legal_moves(*board, patterns, index), request.to_col, request.to_row);
    if (!move) {
        return false;
    }
    Board after = *board;
    play(after, *move);
    write_back(world, *board, after);
    request.has_last = true;
    request.last_from_col = request.from_col;
    request.last_from_row = request.from_row;
    request.last_to_col = request.to_col;
    request.last_to_row = request.to_row;
    return true;
}

std::vector<ecs::Entity> entities_with_requests(ecs::World& world) {
    std::vector<ecs::Entity> found;
    world.each<const MoveRequest>([&](ecs::Entity entity, const MoveRequest& request) {
        if (request.pending) {
            found.push_back(entity);
        }
    });
    return found;
}

/// The oldest request of a grid.
MoveRequest* request_of(ecs::World& world, std::uint32_t grid) {
    MoveRequest* found = nullptr;
    world.each<MoveRequest>([&](ecs::Entity, MoveRequest& request) {
        if (found == nullptr && request.grid == grid) {
            found = &request;
        }
    });
    return found;
}

/// The cell the pointer last touched on a grid, if it touched one in this tick.
std::optional<std::pair<std::int32_t, std::int32_t>> touched_cell(ecs::World& world,
                                                                  std::uint32_t grid) {
    std::optional<std::pair<std::int32_t, std::int32_t>> cell;
    world.each<const grid2d::GridCursor>([&](ecs::Entity entity, const grid2d::GridCursor& cursor) {
        if (!cell && cursor.grid == grid && cursor.clicked && cursor.over &&
            !world.has<transform2d::Disabled>(entity)) {
            cell = std::make_pair(cursor.col, cursor.row);
        }
    });
    return cell;
}

/// What a touch does for a picker: asks for a move, selects a mover, or lets go.
void touch(ecs::World& world, Picker& picker, std::int32_t col, std::int32_t row,
           const TurnInfo& turn) {
    const Patterns patterns = read_patterns(world);
    const auto board = read_board(world, picker.grid, turn.number);
    if (!board) {
        return;
    }
    if (picker.has) {
        const std::ptrdiff_t chosen = board->at(picker.col, picker.row);
        if (chosen >= 0 &&
            move_to(legal_moves(*board, patterns, static_cast<std::size_t>(chosen)), col, row)) {
            if (MoveRequest* request = request_of(world, picker.grid)) {
                request->from_col = picker.col;
                request->from_row = picker.row;
                request->to_col = col;
                request->to_row = row;
                request->pending = true;
            }
            picker.has = false;
            return;
        }
    }
    const std::ptrdiff_t found = board->at(col, row);
    const bool mine =
        found >= 0 &&
        (!turn.present || board->pieces[static_cast<std::size_t>(found)].team == turn.current);
    picker.has = mine;
    if (mine) {
        picker.col = col;
        picker.row = row;
        picker.selected = board->pieces[static_cast<std::size_t>(found)].entity.to_link();
    }
}

/// What the markers of a grid show.
struct View {
    std::vector<bool> quiet;
    std::vector<bool> capture;
    std::optional<std::pair<std::int32_t, std::int32_t>> selected;
    std::optional<std::pair<std::int32_t, std::int32_t>> last_from;
    std::optional<std::pair<std::int32_t, std::int32_t>> last_to;
    std::optional<std::pair<std::int32_t, std::int32_t>> guard;
    grid2d::Layout layout;
};

std::optional<View> view_of(ecs::World& world, std::uint32_t grid, const TurnInfo& turn,
                            const Patterns& patterns) {
    const auto board = read_board(world, grid, turn.number);
    if (!board) {
        return std::nullopt;
    }
    View view;
    view.layout = board->layout;
    const std::size_t cells = static_cast<std::size_t>(board->layout.columns) *
                              static_cast<std::size_t>(board->layout.rows);
    view.quiet.assign(cells, false);
    view.capture.assign(cells, false);
    world.each<const Picker>([&](ecs::Entity entity, const Picker& picker) {
        if (picker.grid != grid || !picker.has || view.selected ||
            world.has<transform2d::Disabled>(entity)) {
            return;
        }
        const std::ptrdiff_t chosen = board->at(picker.col, picker.row);
        if (chosen < 0) {
            return;
        }
        view.selected = std::make_pair(picker.col, picker.row);
        for (const Move& move : legal_moves(*board, patterns, static_cast<std::size_t>(chosen))) {
            const bool takes = move.victim >= 0 || board->at(move.col, move.row) >= 0;
            (takes ? view.capture : view.quiet)[board->layout.index(move.col, move.row)] = true;
        }
    });
    if (const MoveRequest* request = request_of(world, grid);
        request != nullptr && request->has_last) {
        view.last_from = std::make_pair(request->last_from_col, request->last_from_row);
        view.last_to = std::make_pair(request->last_to_col, request->last_to_row);
    }
    for (const Piece& piece : board->pieces) {
        if (piece.guard && !view.guard && guard_in_danger(*board, patterns, piece.team)) {
            const std::vector<bool> danger = threats_against(*board, patterns, piece.team);
            if (danger[board->layout.index(piece.col, piece.row)]) {
                view.guard = std::make_pair(piece.col, piece.row);
            }
        }
    }
    return view;
}

/// Shows or hides a marker, and moves one of those that follow something.
void show(ecs::World& world, ecs::Entity entity, const Marker& marker, grid2d::GridPos& at,
          const View& view) {
    std::optional<std::pair<std::int32_t, std::int32_t>> cell;
    bool visible = false;
    switch (marker.what) {
    case 0:
    case 1: {
        const std::size_t index = view.layout.contains(at.col, at.row)
                                      ? view.layout.index(at.col, at.row)
                                      : view.quiet.size();
        visible = index < view.quiet.size() &&
                  (marker.what == 0U ? view.quiet[index] : view.capture[index]);
        break;
    }
    case 2:
        cell = view.selected;
        break;
    case 3:
        cell = view.last_from;
        break;
    case 4:
        cell = view.last_to;
        break;
    default:
        cell = view.guard;
        break;
    }
    if (cell) {
        at.col = cell->first;
        at.row = cell->second;
        visible = true;
    }
    auto* commands = world.resource<ecs::Commands>();
    const bool hidden = world.has<transform2d::Hidden>(entity);
    if (visible && hidden) {
        commands->remove<transform2d::Hidden>(entity);
    } else if (!visible && !hidden) {
        commands->insert(entity, transform2d::Hidden{});
    }
}

} // namespace

void pick(ecs::World& world) {
    const TurnInfo turn = read_turn(world);
    std::vector<ecs::Entity> pickers;
    world.each<const Picker>([&](ecs::Entity entity, const Picker&) {
        if (!world.has<transform2d::Disabled>(entity)) {
            pickers.push_back(entity);
        }
    });
    for (const ecs::Entity entity : pickers) {
        auto* picker = world.get<Picker>(entity);
        const auto cell = touched_cell(world, picker->grid);
        if (cell) {
            touch(world, *picker, cell->first, cell->second, turn);
        }
    }
}

void execute(ecs::World& world) {
    const TurnInfo turn = read_turn(world);
    const Patterns patterns = read_patterns(world);
    for (const ecs::Entity entity : entities_with_requests(world)) {
        auto* request = world.get<MoveRequest>(entity);
        request->pending = false;
        if (try_play(world, *request, patterns, turn)) {
            ++request->moves;
            if (request->ends_turn) {
                end_turn(world);
            }
            raise(world, request->moved_action);
        } else {
            ++request->refused;
            raise(world, request->refused_action);
        }
    }
}

void judge(ecs::World& world) {
    const TurnInfo turn = read_turn(world);
    const Patterns patterns = read_patterns(world);
    std::vector<ecs::Entity> sides;
    world.each<const Side>([&](ecs::Entity entity, const Side&) { sides.push_back(entity); });
    for (const ecs::Entity entity : sides) {
        auto* side = world.get<Side>(entity);
        const auto board = read_board(world, side->grid, turn.number);
        std::uint32_t status = 0;
        const bool to_move = !turn.present || turn.current == side->player;
        if (to_move && (!board || !can_move(*board, patterns, side->player))) {
            status |= 2U;
        }
        if (board && guard_in_danger(*board, patterns, side->player)) {
            status |= 1U;
        }
        side->status = status;
    }
}

void mark(ecs::World& world) {
    const TurnInfo turn = read_turn(world);
    const Patterns patterns = read_patterns(world);
    std::vector<std::pair<std::uint32_t, View>> views;
    std::vector<ecs::Entity> markers;
    world.each<const Marker, const grid2d::GridPos>(
        [&](ecs::Entity entity, const Marker&, const grid2d::GridPos&) {
            markers.push_back(entity);
        });
    for (const ecs::Entity entity : markers) {
        auto* marker = world.get<Marker>(entity);
        auto* at = world.get<grid2d::GridPos>(entity);
        auto known = std::ranges::find_if(
            views, [&](const auto& item) { return item.first == marker->grid; });
        if (known == views.end()) {
            auto view = view_of(world, marker->grid, turn, patterns);
            if (!view) {
                continue;
            }
            views.emplace_back(marker->grid, std::move(*view));
            known = views.end() - 1;
        }
        show(world, entity, *marker, *at, known->second);
    }
}

} // namespace ge::mod::grid_moves
