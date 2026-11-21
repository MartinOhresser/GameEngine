#pragma once

// What other modules need to know about a board: where its cells are and what sits on them.
// Header only, so a module that depends on grid2d uses it without a call into another module.

#include "ge/ecs/world.hpp"
#include "ge/mod/grid2d/grid2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ge::mod::grid2d {

/// The cell along one axis of a board (count cells of size units, spacing apart, centred on
/// center): -1 for a position that falls in the space between two cells. Off the board, the
/// number of the cell it would be in.
[[nodiscard]] inline std::int32_t cell_along(float position, float center, std::int32_t count,
                                             float size, float spacing) {
    const float pitch = size + spacing;
    const float length = static_cast<float>(count) * size + static_cast<float>(count - 1) * spacing;
    const float from_start = position - (center - length / 2.0F);
    const auto cell = static_cast<std::int32_t>(std::floor(from_start / pitch));
    if (spacing > 0.0F && from_start - static_cast<float>(cell) * pitch >= size) {
        return -1;
    }
    return cell;
}

/// A board as it is laid out: in the world, or on the screen (in pixels of the window).
struct Layout {
    std::uint32_t id = 0;
    std::int32_t columns = 0;
    std::int32_t rows = 0;
    float cell_width = 0.0F;
    float cell_height = 0.0F;
    float center_x = 0.0F;
    float center_y = 0.0F;
    float spacing_x = 0.0F;
    float spacing_y = 0.0F;
    bool screen = false;

    [[nodiscard]] bool contains(std::int32_t col, std::int32_t row) const {
        return col >= 0 && row >= 0 && col < columns && row < rows;
    }
    [[nodiscard]] std::size_t index(std::int32_t col, std::int32_t row) const {
        return static_cast<std::size_t>(row) * static_cast<std::size_t>(columns) +
               static_cast<std::size_t>(col);
    }
    /// The position of the centre of a cell.
    [[nodiscard]] float x_of(std::int32_t col) const {
        return center_x + (static_cast<float>(col) - static_cast<float>(columns - 1) / 2.0F) *
                              (cell_width + spacing_x);
    }
    [[nodiscard]] float y_of(std::int32_t row) const {
        return center_y + (static_cast<float>(row) - static_cast<float>(rows - 1) / 2.0F) *
                              (cell_height + spacing_y);
    }
    /// The cell a position is in, even when it is off the board (check with `contains`); -1
    /// in the space between two cells.
    [[nodiscard]] std::int32_t col_at(float x) const {
        return cell_along(x, center_x, columns, cell_width, spacing_x);
    }
    [[nodiscard]] std::int32_t row_at(float y) const {
        return cell_along(y, center_y, rows, cell_height, spacing_y);
    }
};

/// The board with this id (the oldest one if several have it), or nothing.
inline std::optional<Layout> layout_of(ecs::World& world, std::uint32_t id) {
    std::optional<Layout> found;
    world.each<const Grid, const transform2d::Position>(
        [&](ecs::Entity, const Grid& grid, const transform2d::Position& position) {
            if (!found && grid.id == id && grid.columns > 0U && grid.rows > 0U &&
                grid.cell_width > 0.0F && grid.cell_height > 0.0F) {
                found = Layout{id,
                               static_cast<std::int32_t>(grid.columns),
                               static_cast<std::int32_t>(grid.rows),
                               grid.cell_width,
                               grid.cell_height,
                               position.x,
                               position.y,
                               std::max(grid.spacing_x, 0.0F),
                               std::max(grid.spacing_y, 0.0F),
                               grid.screen};
            }
        });
    return found;
}

/// Who sits on each cell of the board (row by row): the oldest entity with a GridPos there that
/// is not Disabled, or the null entity. Entities off the board are not listed.
inline std::vector<ecs::Entity> occupancy(ecs::World& world, const Layout& layout) {
    std::vector<ecs::Entity> cells(static_cast<std::size_t>(layout.columns) *
                                   static_cast<std::size_t>(layout.rows));
    world.each<const GridPos>([&](ecs::Entity entity, const GridPos& at) {
        if (at.grid != layout.id || !layout.contains(at.col, at.row) ||
            world.has<transform2d::Disabled>(entity)) {
            return;
        }
        ecs::Entity& cell = cells[layout.index(at.col, at.row)];
        if (cell.is_null()) {
            cell = entity;
        }
    });
    return cells;
}

} // namespace ge::mod::grid2d
