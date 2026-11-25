#pragma once

// The cells of the terrains, kept out of the components (a component holds scalars only). Header
// only, so that a module that depends on terrain2d reads and digs the ground without a call into
// another module.

#include "ge/core/hash.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace ge::mod::terrain2d {

/// The value of a solid cell that cannot be dug (any other solid cell is 1).
inline constexpr std::uint8_t hard_cell = 2U;

/// The cells of one terrain: columns by rows squares of side `cell`, the top-left one at (x, y).
struct Grid {
    std::uint32_t columns = 0;
    std::uint32_t rows = 0;
    float cell = 8.0F;
    float x = 0.0F;
    float y = 0.0F;
    std::uint32_t layer = 1U;
    std::vector<std::uint8_t> solid; ///< 1 for a solid cell, hard_cell for one that stays.

    [[nodiscard]] bool inside(std::int64_t column, std::int64_t row) const noexcept {
        return column >= 0 && row >= 0 && column < static_cast<std::int64_t>(columns) &&
               row < static_cast<std::int64_t>(rows);
    }

    [[nodiscard]] std::size_t index(std::int64_t column, std::int64_t row) const noexcept {
        return static_cast<std::size_t>(row) * columns + static_cast<std::size_t>(column);
    }

    /// Whether the cell is solid; outside the grid nothing is.
    [[nodiscard]] bool at(std::int64_t column, std::int64_t row) const noexcept {
        return inside(column, row) && solid[index(column, row)] != 0U;
    }

    /// The column, or row, of a world coordinate (it may be outside the grid).
    [[nodiscard]] std::int64_t column_of(float world_x) const noexcept {
        return static_cast<std::int64_t>(std::floor((world_x - x) / cell));
    }
    [[nodiscard]] std::int64_t row_of(float world_y) const noexcept {
        return static_cast<std::int64_t>(std::floor((world_y - y) / cell));
    }

    /// Whether any solid cell overlaps the box (its right and bottom edges excluded).
    [[nodiscard]] bool any_in(float left, float top, float right, float bottom) const noexcept {
        const float shave = cell * 0.001F;
        const std::int64_t first_column = std::max<std::int64_t>(column_of(left), 0);
        const std::int64_t last_column =
            std::min<std::int64_t>(column_of(right - shave), static_cast<std::int64_t>(columns) - 1);
        const std::int64_t first_row = std::max<std::int64_t>(row_of(top), 0);
        const std::int64_t last_row =
            std::min<std::int64_t>(row_of(bottom - shave), static_cast<std::int64_t>(rows) - 1);
        for (std::int64_t row = first_row; row <= last_row; row += 1) {
            for (std::int64_t column = first_column; column <= last_column; column += 1) {
                if (solid[index(column, row)] != 0U) {
                    return true;
                }
            }
        }
        return false;
    }

    /// Empties the cells whose middle is within radius of (cx, cy), but hard ones; returns how many
    /// it emptied.
    std::uint32_t carve(float cx, float cy, float radius) noexcept {
        if (radius <= 0.0F) {
            return 0U;
        }
        const std::int64_t first_column = std::max<std::int64_t>(column_of(cx - radius), 0);
        const std::int64_t last_column =
            std::min<std::int64_t>(column_of(cx + radius), static_cast<std::int64_t>(columns) - 1);
        const std::int64_t first_row = std::max<std::int64_t>(row_of(cy - radius), 0);
        const std::int64_t last_row =
            std::min<std::int64_t>(row_of(cy + radius), static_cast<std::int64_t>(rows) - 1);
        std::uint32_t carved = 0U;
        for (std::int64_t row = first_row; row <= last_row; row += 1) {
            for (std::int64_t column = first_column; column <= last_column; column += 1) {
                const float mx = x + (static_cast<float>(column) + 0.5F) * cell - cx;
                const float my = y + (static_cast<float>(row) + 0.5F) * cell - cy;
                std::uint8_t& value = solid[index(column, row)];
                if (value == 1U && mx * mx + my * my <= radius * radius) {
                    value = 0U;
                    carved += 1U;
                }
            }
        }
        return carved;
    }

    void hash(Hasher& hasher) const {
        hasher.add(columns);
        hasher.add(rows);
        hasher.add(cell);
        hasher.add(x);
        hasher.add(y);
        hasher.add(layer);
        for (const std::uint8_t value : solid) {
            hasher.add(value);
        }
    }
};

/// Every terrain's cells, by the link of its entity (`Entity::to_link`). A world resource.
struct Cells {
    std::map<std::uint64_t, Grid> grids;

    void hash(Hasher& hasher) const {
        for (const auto& [link, grid] : grids) {
            hasher.add(link);
            grid.hash(hasher);
        }
    }
};

} // namespace ge::mod::terrain2d
