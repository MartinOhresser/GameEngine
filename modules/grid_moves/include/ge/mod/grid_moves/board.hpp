#pragma once

// The rules of moving on a board, as plain functions over a snapshot of it: the moves a piece
// may make, the cells a side threatens, whether a Guard is in danger. Header only, so a module
// that depends on grid_moves (a computer opponent, say) can use them without calling another
// module. Nothing here knows a game: a piece is a kind, a side, a cell and a few counters.

#include "ge/ecs/world.hpp"
#include "ge/mod/grid2d/cells.hpp"
#include "ge/mod/grid2d/grid2d.gen.hpp"
#include "ge/mod/grid_moves/grid_moves.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"
#include "ge/mod/turns/turns.gen.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <vector>

namespace ge::mod::grid_moves {

inline constexpr std::uint32_t none = 0xFFFFFFFFU;
inline constexpr std::uint32_t mode_move_only = 1;
inline constexpr std::uint32_t mode_capture_only = 2;

using Patterns = std::vector<Pattern>;

/// A mover on the board.
struct Piece {
    ecs::Entity entity;
    std::uint32_t team = 0;
    std::uint32_t kind = 0;
    std::int32_t col = 0;
    std::int32_t row = 0;
    std::uint32_t moves_made = 0;
    std::uint32_t last_turn = none;
    std::uint32_t last_distance = 0;
    bool guard = false;
    bool alive = true;
};

/// A move of a piece (an index into `Board::pieces`) to a cell, with what goes with it.
struct Move {
    std::size_t piece = 0;
    std::int32_t col = 0;
    std::int32_t row = 0;
    std::ptrdiff_t victim = -1;    // a piece taken elsewhere than on the destination
    std::ptrdiff_t companion = -1; // a second piece that moves too
    std::int32_t companion_col = 0;
    std::int32_t companion_row = 0;
};

/// The pieces of a board and its walls, with the number of the turn they are played in.
class Board {
public:
    explicit Board(const grid2d::Layout& shape)
        : layout(shape),
          walls_(static_cast<std::size_t>(shape.columns) * static_cast<std::size_t>(shape.rows),
                 false),
          cells_(walls_.size(), -1) {}

    grid2d::Layout layout;
    std::vector<Piece> pieces;
    std::uint32_t turn_number = 0;

    [[nodiscard]] bool inside(std::int32_t col, std::int32_t row) const {
        return layout.contains(col, row);
    }
    [[nodiscard]] bool wall(std::int32_t col, std::int32_t row) const {
        return walls_[layout.index(col, row)];
    }
    void set_wall(std::int32_t col, std::int32_t row) { walls_[layout.index(col, row)] = true; }
    /// The index of the piece on a cell, or -1.
    [[nodiscard]] std::ptrdiff_t at(std::int32_t col, std::int32_t row) const {
        return cells_[layout.index(col, row)];
    }
    /// Must be called after the pieces are listed or moved.
    void reindex() {
        std::ranges::fill(cells_, -1);
        for (std::size_t i = 0; i < pieces.size(); ++i) {
            const Piece& piece = pieces[i];
            if (piece.alive && inside(piece.col, piece.row) &&
                cells_[layout.index(piece.col, piece.row)] < 0) {
                cells_[layout.index(piece.col, piece.row)] = static_cast<std::ptrdiff_t>(i);
            }
        }
    }

private:
    std::vector<bool> walls_;
    std::vector<std::ptrdiff_t> cells_;
};

/// A pattern is for a piece when kind and player match, and it is not a first move that the
/// piece is past.
[[nodiscard]] inline bool applies(const Pattern& pattern, const Piece& piece) {
    return pattern.kind == piece.kind && (pattern.player == none || pattern.player == piece.team) &&
           (!pattern.first_move_only || piece.moves_made == 0U);
}

/// Visits the cells a pattern reaches from a piece, with the step number and the piece that
/// stands there (-1 for none), until `visit` says stop, a piece is met, or the board ends.
template <typename Visit>
void walk(const Board& board, const Pattern& pattern, const Piece& piece, Visit visit) {
    if (pattern.jump) {
        const std::int32_t col = piece.col + pattern.dx;
        const std::int32_t row = piece.row + pattern.dy;
        if (board.inside(col, row) && !board.wall(col, row)) {
            static_cast<void>(visit(col, row, 1U, board.at(col, row)));
        }
        return;
    }
    if (pattern.dx == 0 && pattern.dy == 0) {
        return;
    }
    for (std::uint32_t step = 1; pattern.range == 0U || step <= pattern.range; ++step) {
        const auto along = static_cast<std::int32_t>(step);
        const std::int32_t col = piece.col + pattern.dx * along;
        const std::int32_t row = piece.row + pattern.dy * along;
        if (!board.inside(col, row) || board.wall(col, row)) {
            return;
        }
        const std::ptrdiff_t occupant = board.at(col, row);
        if (!visit(col, row, step, occupant) || occupant >= 0) {
            return;
        }
    }
}

/// The cells that the pieces of every side but `team` threaten (cells they could capture on).
[[nodiscard]] inline std::vector<bool> threats_against(const Board& board, const Patterns& patterns,
                                                       std::uint32_t team) {
    std::vector<bool> danger(static_cast<std::size_t>(board.layout.columns) *
                                 static_cast<std::size_t>(board.layout.rows),
                             false);
    for (const Piece& piece : board.pieces) {
        if (!piece.alive || piece.team == team) {
            continue;
        }
        for (const Pattern& pattern : patterns) {
            if (!applies(pattern, piece) || pattern.mode == mode_move_only) {
                continue;
            }
            walk(board, pattern, piece,
                 [&](std::int32_t col, std::int32_t row, std::uint32_t step, std::ptrdiff_t) {
                     if (step >= pattern.min_range) {
                         danger[board.layout.index(col, row)] = true;
                     }
                     return true;
                 });
        }
    }
    return danger;
}

/// Whether a Guard of this side stands on a threatened cell.
[[nodiscard]] inline bool guard_in_danger(const Board& board, const Patterns& patterns,
                                          std::uint32_t team) {
    const std::vector<bool> danger = threats_against(board, patterns, team);
    return std::ranges::any_of(board.pieces, [&](const Piece& piece) {
        return piece.alive && piece.guard && piece.team == team &&
               danger[board.layout.index(piece.col, piece.row)];
    });
}

namespace detail {

/// The opposing piece that a victim rule takes, if the conditions hold.
[[nodiscard]] inline std::ptrdiff_t find_victim(const Board& board, const Pattern& pattern,
                                                const Piece& mover, std::int32_t col,
                                                std::int32_t row) {
    const std::int32_t at_col = col + pattern.victim_dx;
    const std::int32_t at_row = row + pattern.victim_dy;
    if (!board.inside(at_col, at_row)) {
        return -1;
    }
    const std::ptrdiff_t found = board.at(at_col, at_row);
    if (found < 0) {
        return -1;
    }
    const Piece& other = board.pieces[static_cast<std::size_t>(found)];
    const bool recent = other.last_turn != none && other.last_turn + 1U == board.turn_number &&
                        other.last_distance >= pattern.victim_min_distance;
    return other.team != mover.team && other.kind == pattern.victim_kind && recent ? found : -1;
}

/// The piece that goes along with a move, if it is there and has not moved, with the cells
/// between it and the mover empty.
[[nodiscard]] inline std::ptrdiff_t find_companion(const Board& board, const Pattern& pattern,
                                                   const Piece& mover) {
    if (pattern.companion_from == 0U || pattern.companion_to >= pattern.companion_from) {
        return -1;
    }
    const auto from = static_cast<std::int32_t>(pattern.companion_from);
    for (std::int32_t step = 1; step < from; ++step) {
        const std::int32_t col = mover.col + pattern.dx * step;
        const std::int32_t row = mover.row + pattern.dy * step;
        if (!board.inside(col, row) || board.wall(col, row) || board.at(col, row) >= 0) {
            return -1;
        }
    }
    const std::int32_t col = mover.col + pattern.dx * from;
    const std::int32_t row = mover.row + pattern.dy * from;
    if (!board.inside(col, row)) {
        return -1;
    }
    const std::ptrdiff_t found = board.at(col, row);
    if (found < 0) {
        return -1;
    }
    const Piece& other = board.pieces[static_cast<std::size_t>(found)];
    return other.team == mover.team && other.kind == pattern.companion_kind &&
                   other.moves_made == 0U
               ? found
               : -1;
}

/// The move that a pattern offers on a cell it reaches, if any.
[[nodiscard]] inline std::optional<Move> offer(const Board& board, const Pattern& pattern,
                                               std::size_t index, std::int32_t col,
                                               std::int32_t row, std::ptrdiff_t occupant) {
    const Piece& mover = board.pieces[index];
    Move move;
    move.piece = index;
    move.col = col;
    move.row = row;
    if (occupant >= 0) {
        const bool opposing = board.pieces[static_cast<std::size_t>(occupant)].team != mover.team;
        const bool plain = pattern.victim_kind == none && pattern.companion_kind == none;
        return opposing && plain && pattern.mode != mode_move_only ? std::optional<Move>{move}
                                                                   : std::nullopt;
    }
    if (pattern.mode == mode_capture_only) {
        return std::nullopt;
    }
    if (pattern.victim_kind != none) {
        move.victim = find_victim(board, pattern, mover, col, row);
        if (move.victim < 0) {
            return std::nullopt;
        }
    }
    if (pattern.companion_kind != none) {
        move.companion = find_companion(board, pattern, mover);
        if (move.companion < 0) {
            return std::nullopt;
        }
        const auto to = static_cast<std::int32_t>(pattern.companion_to);
        move.companion_col = mover.col + pattern.dx * to;
        move.companion_row = mover.row + pattern.dy * to;
    }
    return move;
}

} // namespace detail

/// Every move a piece may make by its patterns, without looking at what is left under threat.
/// `danger` (from `threats_against`) is what the safe_path patterns check.
[[nodiscard]] inline std::vector<Move> pseudo_moves(const Board& board, const Patterns& patterns,
                                                    std::size_t index,
                                                    const std::vector<bool>& danger) {
    std::vector<Move> moves;
    const Piece& piece = board.pieces[index];
    for (const Pattern& pattern : patterns) {
        if (!applies(pattern, piece) ||
            (pattern.safe_path && danger[board.layout.index(piece.col, piece.row)])) {
            continue;
        }
        walk(board, pattern, piece,
             [&](std::int32_t col, std::int32_t row, std::uint32_t step, std::ptrdiff_t occupant) {
                 if (pattern.safe_path && danger[board.layout.index(col, row)]) {
                     return false;
                 }
                 if (step >= pattern.min_range) {
                     if (const auto move =
                             detail::offer(board, pattern, index, col, row, occupant)) {
                         moves.push_back(*move);
                     }
                 }
                 return true;
             });
    }
    return moves;
}

/// The board after a move: the piece is on its new cell, what it takes is gone, a companion
/// has moved too, and their counters are up to date.
inline void play(Board& board, const Move& move) {
    Piece& mover = board.pieces[move.piece];
    const auto distance = static_cast<std::uint32_t>(
        std::max(std::abs(move.col - mover.col), std::abs(move.row - mover.row)));
    const std::ptrdiff_t taken = board.at(move.col, move.row);
    if (taken >= 0 && static_cast<std::size_t>(taken) != move.piece) {
        board.pieces[static_cast<std::size_t>(taken)].alive = false;
    }
    if (move.victim >= 0) {
        board.pieces[static_cast<std::size_t>(move.victim)].alive = false;
    }
    mover.col = move.col;
    mover.row = move.row;
    ++mover.moves_made;
    mover.last_turn = board.turn_number;
    mover.last_distance = distance;
    if (move.companion >= 0) {
        Piece& other = board.pieces[static_cast<std::size_t>(move.companion)];
        other.last_distance = static_cast<std::uint32_t>(std::max(
            std::abs(move.companion_col - other.col), std::abs(move.companion_row - other.row)));
        other.col = move.companion_col;
        other.row = move.companion_row;
        ++other.moves_made;
        other.last_turn = board.turn_number;
    }
    board.reindex();
}

/// The moves a piece may make: those of its patterns that do not leave a Guard of its side
/// threatened.
[[nodiscard]] inline std::vector<Move> legal_moves(const Board& board, const Patterns& patterns,
                                                   std::size_t index) {
    const std::uint32_t team = board.pieces[index].team;
    std::vector<Move> moves =
        pseudo_moves(board, patterns, index, threats_against(board, patterns, team));
    std::erase_if(moves, [&](const Move& move) {
        Board next = board;
        play(next, move);
        return guard_in_danger(next, patterns, team);
    });
    return moves;
}

/// Whether the side has any legal move.
[[nodiscard]] inline bool can_move(const Board& board, const Patterns& patterns,
                                   std::uint32_t team) {
    for (std::size_t i = 0; i < board.pieces.size(); ++i) {
        if (board.pieces[i].alive && board.pieces[i].team == team &&
            !legal_moves(board, patterns, i).empty()) {
            return true;
        }
    }
    return false;
}

/// The turn of the game, if it has one.
struct TurnInfo {
    bool present = false;
    std::uint32_t players = 1;
    std::uint32_t current = 0;
    std::uint32_t number = 0;
};

[[nodiscard]] inline TurnInfo read_turn(ecs::World& world) {
    TurnInfo info;
    world.each<const turns::Turn>([&](ecs::Entity, const turns::Turn& turn) {
        if (!info.present) {
            info = TurnInfo{true, std::max(turn.players, 1U), turn.current, turn.number};
        }
    });
    return info;
}

[[nodiscard]] inline Patterns read_patterns(ecs::World& world) {
    Patterns patterns;
    world.each<const Pattern>(
        [&](ecs::Entity, const Pattern& pattern) { patterns.push_back(pattern); });
    return patterns;
}

/// The pieces and walls of the board of this grid id, in the order of their entities.
[[nodiscard]] inline std::optional<Board> read_board(ecs::World& world, std::uint32_t grid,
                                                     std::uint32_t turn_number) {
    const auto layout = grid2d::layout_of(world, grid);
    if (!layout) {
        return std::nullopt;
    }
    Board board(*layout);
    board.turn_number = turn_number;
    world.each<const Mover, const turns::Team, const grid2d::GridPos>(
        [&](ecs::Entity entity, const Mover& mover, const turns::Team& team,
            const grid2d::GridPos& at) {
            if (at.grid == grid && layout->contains(at.col, at.row) &&
                !world.has<transform2d::Disabled>(entity)) {
                board.pieces.push_back(Piece{entity, team.player, mover.kind, at.col, at.row,
                                             mover.moves_made, mover.last_move_turn,
                                             mover.last_move_distance, world.has<Guard>(entity),
                                             true});
            }
        });
    world.each<const grid2d::GridPos, const grid2d::Blocked>(
        [&](ecs::Entity entity, const grid2d::GridPos& at, const grid2d::Blocked&) {
            if (at.grid == grid && layout->contains(at.col, at.row) &&
                !world.has<transform2d::Disabled>(entity)) {
                board.set_wall(at.col, at.row);
            }
        });
    board.reindex();
    return board;
}

} // namespace ge::mod::grid_moves
