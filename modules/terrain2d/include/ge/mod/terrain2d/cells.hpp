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
    /// How many cells around each one the ground is smoothed over for blocks (0: the cells).
    std::uint32_t blur = 0U;
    /// The average of the cells within blur of each cell, kept by refresh (derived from solid).
    std::vector<float> field;

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

    /// Whether the ground stops a box: with blur 0, any solid cell overlapping it; with blur, the
    /// ground as a drawing smoothed over blur cells shows it (terrain2d.Look smooth): solid where
    /// the cells within blur of a cell's middle are more than half solid, bilinearly between the
    /// middles, so that steps of cells are slopes; hard cells stop it whatever the average (the
    /// edges of a world). The field must be up to date (refresh).
    [[nodiscard]] bool blocks(float left, float top, float right, float bottom) const noexcept {
        if (blur == 0U || field.size() != solid.size()) {
            return any_in(left, top, right, bottom);
        }
        if (any_hard_in(left, top, right, bottom)) {
            return true;
        }
        // Bilinear between middles: its largest value over the box is at a corner of the box, a
        // middle inside it, or where its edges cross the lines through the middles.
        const float u0 = ((left - x) / cell) - 0.5F;
        const float u1 = ((right - x) / cell) - 0.5F;
        const float w0 = ((top - y) / cell) - 0.5F;
        const float w1 = ((bottom - y) / cell) - 0.5F;
        const auto first_u = static_cast<std::int64_t>(std::floor(u0)) + 1;
        const auto last_u = static_cast<std::int64_t>(std::ceil(u1)) - 1;
        const auto first_w = static_cast<std::int64_t>(std::floor(w0)) + 1;
        const auto last_w = static_cast<std::int64_t>(std::ceil(w1)) - 1;
        for (std::int64_t i = first_w - 1; i <= last_w + 1; i += 1) {
            const float w = i < first_w ? w0 : (i > last_w ? w1 : static_cast<float>(i));
            for (std::int64_t k = first_u - 1; k <= last_u + 1; k += 1) {
                const float u = k < first_u ? u0 : (k > last_u ? u1 : static_cast<float>(k));
                if (field_at(u, w) >= 0.5F) {
                    return true;
                }
            }
        }
        return false;
    }

    /// The smoothed ground at (u, w), in cells from the middle of the first one: 0 open, 1 solid.
    [[nodiscard]] float field_at(float u, float w) const noexcept {
        const auto c = static_cast<std::int64_t>(std::floor(u));
        const auto r = static_cast<std::int64_t>(std::floor(w));
        const float tu = u - static_cast<float>(c);
        const float tw = w - static_cast<float>(r);
        const float top = averaged(c, r) + ((averaged(c + 1, r) - averaged(c, r)) * tu);
        const float bottom =
            averaged(c, r + 1) + ((averaged(c + 1, r + 1) - averaged(c, r + 1)) * tu);
        return top + ((bottom - top) * tw);
    }

    /// The average of the cells within blur of a cell (0 outside the grid).
    [[nodiscard]] float averaged(std::int64_t column, std::int64_t row) const noexcept {
        return inside(column, row) ? field[index(column, row)] : 0.0F;
    }

    /// Recomputes the averages of the cells within blur of the columns and rows given (all the
    /// grid when they cover it), after cells changed.
    void refresh(std::int64_t first_column, std::int64_t first_row, std::int64_t last_column,
                 std::int64_t last_row) {
        if (blur == 0U) {
            return;
        }
        field.resize(solid.size(), 0.0F);
        const auto span = static_cast<std::int64_t>(blur);
        const float count = static_cast<float>(((2 * span) + 1) * ((2 * span) + 1));
        const std::int64_t c0 = std::max<std::int64_t>(first_column - span, 0);
        const std::int64_t c1 =
            std::min<std::int64_t>(last_column + span, static_cast<std::int64_t>(columns) - 1);
        const std::int64_t r0 = std::max<std::int64_t>(first_row - span, 0);
        const std::int64_t r1 =
            std::min<std::int64_t>(last_row + span, static_cast<std::int64_t>(rows) - 1);
        for (std::int64_t row = r0; row <= r1; row += 1) {
            for (std::int64_t column = c0; column <= c1; column += 1) {
                field[index(column, row)] =
                    static_cast<float>(solid_around(column, row, span)) / count;
            }
        }
    }

    void refresh_all() {
        refresh(0, 0, static_cast<std::int64_t>(columns) - 1, static_cast<std::int64_t>(rows) - 1);
    }

    /// Whether a hard cell overlaps the box (its right and bottom edges excluded).
    [[nodiscard]] bool any_hard_in(float left, float top, float right, float bottom) const noexcept {
        const float shave = cell * 0.001F;
        const std::int64_t first_column = std::max<std::int64_t>(column_of(left), 0);
        const std::int64_t last_column =
            std::min<std::int64_t>(column_of(right - shave), static_cast<std::int64_t>(columns) - 1);
        const std::int64_t first_row = std::max<std::int64_t>(row_of(top), 0);
        const std::int64_t last_row =
            std::min<std::int64_t>(row_of(bottom - shave), static_cast<std::int64_t>(rows) - 1);
        for (std::int64_t row = first_row; row <= last_row; row += 1) {
            for (std::int64_t column = first_column; column <= last_column; column += 1) {
                if (solid[index(column, row)] == hard_cell) {
                    return true;
                }
            }
        }
        return false;
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
        if (carved > 0U) {
            refresh(first_column, first_row, last_column, last_row);
        }
        return carved;
    }

    /// After a dig around (cx, cy) within radius: the cells near it that are mostly surrounded by
    /// open ones (fewer than half of the cells within reach of them solid, the square a smoothed
    /// drawing averages) are carved too, as a drawing smoothed over reach cells would not show
    /// them and they must not stop anything. Hard cells stay. Returns how many went.
    std::uint32_t crumble(float cx, float cy, float radius, std::uint32_t reach) noexcept {
        if (reach == 0U || radius <= 0.0F) {
            return 0U;
        }
        const auto span = static_cast<std::int64_t>(reach);
        const float around = radius + static_cast<float>(span + 1) * cell;
        const std::int64_t first_column = std::max<std::int64_t>(column_of(cx - around), 0);
        const std::int64_t last_column =
            std::min<std::int64_t>(column_of(cx + around), static_cast<std::int64_t>(columns) - 1);
        const std::int64_t first_row = std::max<std::int64_t>(row_of(cy - around), 0);
        const std::int64_t last_row =
            std::min<std::int64_t>(row_of(cy + around), static_cast<std::int64_t>(rows) - 1);
        const std::int64_t side = (2 * span) + 1;
        const std::int64_t needed = (side * side + 1) / 2; // at least half of the square
        std::uint32_t removed = 0U;
        // Twice: what one crumb held up may crumble once it is gone.
        for (int pass = 0; pass < 2; pass += 1) {
            for (std::int64_t row = first_row; row <= last_row; row += 1) {
                for (std::int64_t column = first_column; column <= last_column; column += 1) {
                    std::uint8_t& value = solid[index(column, row)];
                    if (value != 1U || solid_around(column, row, span) >= needed) {
                        continue;
                    }
                    value = 0U;
                    removed += 1U;
                }
            }
        }
        if (removed > 0U) {
            refresh(first_column, first_row, last_column, last_row);
        }
        return removed;
    }

    /// How many cells are solid in the square of cells within span of (column, row).
    [[nodiscard]] std::int64_t solid_around(std::int64_t column, std::int64_t row,
                                            std::int64_t span) const noexcept {
        std::int64_t count = 0;
        for (std::int64_t r = row - span; r <= row + span; r += 1) {
            for (std::int64_t c = column - span; c <= column + span; c += 1) {
                count += at(c, r) ? 1 : 0;
            }
        }
        return count;
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
