#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/ecs/commands.hpp"
#include "ge/mod/grid2d/cells.hpp"
#include "ge/mod/grid2d/grid2d.gen.hpp"
#include "ge/mod/slide_merge/slide_merge.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ge::mod::slide_merge {

namespace {

constexpr std::uint32_t merge_next = 1;
constexpr std::uint32_t merge_none = 2;

/// What sits on a cell of the board.
struct Slot {
    ecs::Entity entity;
    std::int32_t value = 0;
    bool tile = false;
    bool obstacle = false;
};

/// What happens to one tile in a slide: it goes from a cell to another (cell indexes, row by
/// row). A tile that is absorbed goes to the cell of the tile it merges into.
struct Step {
    ecs::Entity entity;
    std::size_t from = 0;
    std::size_t to = 0;
    std::int32_t value = 0;
    bool absorbed = false;
    bool grew = false;
};

/// The direction of a slide: the step in columns and in rows.
struct Direction {
    std::int32_t col = 0;
    std::int32_t row = 0;
};

// The order of the actions of a board: left, right, up, down.
constexpr std::array<Direction, 4> directions = {Direction{-1, 0}, Direction{1, 0},
                                                 Direction{0, -1}, Direction{0, 1}};

std::int32_t merged_value(std::uint32_t mode, std::int32_t value) {
    return mode == merge_next ? value + 1 : value * 2;
}

bool pressed(const app::Actions* actions, const app::ActionNames* names, std::uint32_t action) {
    return actions != nullptr && names != nullptr && action < names->names.size() &&
           actions->pressed(names->names[action]);
}

void raise(app::Actions* actions, const app::ActionNames* names, std::uint32_t action) {
    if (actions != nullptr && names != nullptr && action < names->names.size()) {
        actions->raise(names->names[action]);
    }
}

/// What sits on each cell of the board: tiles (not those being merged away) and obstacles (any
/// other entity on the board, Disabled ones excepted). The oldest entity on a cell counts.
std::vector<Slot> read_slots(ecs::World& world, const grid2d::Layout& layout) {
    std::vector<Slot> slots(static_cast<std::size_t>(layout.columns) *
                            static_cast<std::size_t>(layout.rows));
    world.each<const grid2d::GridPos>([&](ecs::Entity entity, const grid2d::GridPos& at) {
        if (at.grid != layout.id || !layout.contains(at.col, at.row) ||
            world.has<transform2d::Disabled>(entity)) {
            return;
        }
        Slot& slot = slots[layout.index(at.col, at.row)];
        if (slot.tile || slot.obstacle) {
            return;
        }
        const Tile* tile = world.get<Tile>(entity);
        if (tile == nullptr) {
            slot.obstacle = true;
        } else if (!tile->dying) {
            slot.tile = true;
            slot.entity = entity;
            slot.value = tile->value;
        }
    });
    return slots;
}

/// The cells of a line of the board, from the edge the tiles slide towards.
std::vector<std::size_t> line_of(const grid2d::Layout& layout, const Direction& direction,
                                 std::int32_t line) {
    const bool horizontal = direction.col != 0;
    const std::int32_t length = horizontal ? layout.columns : layout.rows;
    const std::int32_t sign = horizontal ? direction.col : direction.row;
    std::vector<std::size_t> cells;
    for (std::int32_t i = 0; i < length; ++i) {
        const std::int32_t along = sign < 0 ? i : length - 1 - i;
        cells.push_back(horizontal ? layout.index(along, line) : layout.index(line, along));
    }
    return cells;
}

/// Slides the tiles of one line: each one goes as far as it can, and joins the one in front of
/// it when they merge (a tile that has just grown does not merge again in the same move).
void slide_line(const std::vector<std::size_t>& cells, const std::vector<Slot>& slots,
                std::uint32_t mode, std::vector<Step>& steps) {
    std::size_t free = 0;
    std::optional<std::size_t> last;
    for (std::size_t k = 0; k < cells.size(); ++k) {
        const Slot& slot = slots[cells[k]];
        if (slot.obstacle) {
            free = k + 1;
            last.reset();
        } else if (slot.tile) {
            if (mode != merge_none && last && !steps[*last].grew &&
                steps[*last].value == slot.value) {
                const std::size_t target = steps[*last].to;
                steps[*last].grew = true;
                steps[*last].value = merged_value(mode, slot.value);
                steps.push_back({slot.entity, cells[k], target, slot.value, true, false});
            } else {
                steps.push_back({slot.entity, cells[k], cells[free], slot.value, false, false});
                last = steps.size() - 1;
                ++free;
            }
        }
    }
}

std::vector<Step> plan(const grid2d::Layout& layout, const std::vector<Slot>& slots,
                       std::uint32_t mode, const Direction& direction) {
    std::vector<Step> steps;
    const std::int32_t lines = direction.col != 0 ? layout.rows : layout.columns;
    for (std::int32_t line = 0; line < lines; ++line) {
        slide_line(line_of(layout, direction, line), slots, mode, steps);
    }
    return steps;
}

bool changes_something(const std::vector<Step>& steps) {
    return std::ranges::any_of(steps, [](const Step& step) { return step.from != step.to; });
}

/// The direction pressed in this tick (the first of left, right, up, down), if any.
std::optional<Direction> pressed_direction(const SlideBoard& board, const app::Actions* actions,
                                           const app::ActionNames* names) {
    const std::array<std::uint32_t, 4> keys = {board.left, board.right, board.up, board.down};
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (pressed(actions, names, keys[i])) {
            return directions[i];
        }
    }
    return std::nullopt;
}

/// Puts the tiles where the slide takes them, and counts the score.
void apply(ecs::World& world, const grid2d::Layout& layout, SlideBoard& board,
           const std::vector<Step>& steps) {
    auto* commands = world.resource<ecs::Commands>();
    for (const Step& step : steps) {
        auto* at = world.get<grid2d::GridPos>(step.entity);
        auto* tile = world.get<Tile>(step.entity);
        if (at == nullptr || tile == nullptr) {
            continue;
        }
        at->col = static_cast<std::int32_t>(step.to % static_cast<std::size_t>(layout.columns));
        at->row = static_cast<std::int32_t>(step.to / static_cast<std::size_t>(layout.columns));
        if (step.absorbed) {
            tile->dying = true;
            tile->merged = true;
            tile->vanish_in = std::max(board.vanish_ticks, 1U);
            commands->insert(step.entity, transform2d::Disabled{});
        } else if (step.grew) {
            tile->value = step.value;
            tile->merged = true;
            board.score += step.value;
            if (board.pop > 0.0F) {
                commands->insert(step.entity, transform2d::Scale{board.pop});
            }
        }
    }
}

void slide_board(ecs::World& world, ecs::Entity entity, const app::ActionNames* names) {
    auto* actions = world.resource<app::Actions>();
    auto* board = world.get<SlideBoard>(entity);
    if (board == nullptr || world.has<transform2d::Disabled>(entity)) {
        return;
    }
    const auto direction = pressed_direction(*board, actions, names);
    const auto layout = grid2d::layout_of(world, board->grid);
    if (!direction || !layout) {
        return;
    }
    const std::vector<Step> steps =
        plan(*layout, read_slots(world, *layout), board->mode, *direction);
    if (!changes_something(steps)) {
        return;
    }
    apply(world, *layout, *board, steps);
    ++board->moves;
    raise(actions, names, board->moved_action);
}

std::vector<ecs::Entity> boards_of(ecs::World& world) {
    std::vector<ecs::Entity> boards;
    world.each<const SlideBoard>(
        [&](ecs::Entity entity, const SlideBoard&) { boards.push_back(entity); });
    return boards;
}

void survey(ecs::World& world, ecs::Entity entity, const app::ActionNames* names) {
    auto* board = world.get<SlideBoard>(entity);
    const auto layout = board == nullptr ? std::nullopt : grid2d::layout_of(world, board->grid);
    if (!layout) {
        return;
    }
    const std::vector<Slot> slots = read_slots(world, *layout);
    std::int32_t highest = 0;
    bool any_tile = false;
    for (const Slot& slot : slots) {
        if (slot.tile) {
            any_tile = true;
            highest = std::max(highest, slot.value);
        }
    }
    const bool can_move = std::ranges::any_of(directions, [&](const Direction& direction) {
        return changes_something(plan(*layout, slots, board->mode, direction));
    });
    const bool stuck = any_tile && !can_move;
    board->highest = highest;
    if (stuck && !board->no_moves) {
        raise(world.resource<app::Actions>(), names, board->stuck_action);
    }
    board->no_moves = stuck;
}

} // namespace

void slide(ecs::World& world) {
    const auto* names = world.resource<app::ActionNames>();
    world.each<Tile>([](ecs::Entity, Tile& tile) { tile.merged = false; });
    for (const ecs::Entity entity : boards_of(world)) {
        slide_board(world, entity, names);
    }
}

void tally(ecs::World& world) {
    const auto* names = world.resource<app::ActionNames>();
    auto* commands = world.resource<ecs::Commands>();
    world.each<Tile>([&](ecs::Entity entity, Tile& tile) {
        if (!tile.dying || tile.merged) {
            return;
        }
        if (tile.vanish_in > 0U) {
            --tile.vanish_in;
        }
        if (tile.vanish_in == 0U) {
            commands->despawn(entity);
        }
    });
    for (const ecs::Entity entity : boards_of(world)) {
        survey(world, entity, names);
    }
}

} // namespace ge::mod::slide_merge
