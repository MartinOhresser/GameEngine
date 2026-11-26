// The pictures of the terrains: each terrain is cut into square parts of cells, and each part
// becomes a picture made while the game runs (Renderer::set_image), remade only when one of the
// cells it shows changes. Presentation only: nothing here is part of the world's state.

#include "ge/app/render.hpp"
#include "ge/mod/sprite2d/layered.hpp"
#include "ge/mod/terrain2d/cells.hpp"
#include "ge/mod/terrain2d/terrain2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace ge::mod::terrain2d {

namespace {

/// The side of a part, in cells.
constexpr std::int64_t part_cells = 64;

struct Part {
    int image = 0;
    std::vector<std::uint8_t> seen; ///< The cells the picture was made from (with a border).
    bool empty = true;
};

struct Drawn {
    std::uint32_t revision = 0xFFFFFFFFU;
    std::int64_t columns = 0; ///< Parts across, and down.
    std::int64_t rows = 0;
    std::vector<Part> parts;
};

/// The pictures of every terrain, by the link of its entity. Presentation: not hashed.
struct Pictures {
    std::map<std::uint64_t, Drawn> drawn;
    int next_image = app::Renderer::first_made_image;
    void hash(Hasher& /*hasher*/) const noexcept {}
};

Pictures& pictures_of(ecs::World& world) {
    if (auto* pictures = world.resource<Pictures>(); pictures != nullptr) {
        return *pictures;
    }
    return world.emplace_resource<Pictures>();
}

std::uint32_t channel(std::uint32_t value) { return std::min(value, 255U); }

/// The cells around a part as numbers, 1 solid and 0 open, each averaged with the cells within
/// smooth of it (a square of 2 * smooth + 1 cells): steps of one cell along a gentle slope become
/// a slope. Outside what it keeps, a cell counts as open.
class Solid {
public:
    Solid(const Grid& grid, std::int64_t first_column, std::int64_t first_row,
          std::int64_t columns, std::int64_t rows, std::int64_t smooth)
        : first_column_(first_column), first_row_(first_row), columns_(columns), rows_(rows),
          values_(static_cast<std::size_t>(columns * rows), 0.0F) {
        // The cells themselves, with smooth more around, then averaged across and down.
        const std::int64_t wide = columns + 2 * smooth;
        const std::int64_t tall = rows + 2 * smooth;
        std::vector<float> raw(static_cast<std::size_t>(wide * tall), 0.0F);
        for (std::int64_t row = 0; row < tall; row += 1) {
            for (std::int64_t column = 0; column < wide; column += 1) {
                raw[static_cast<std::size_t>(row * wide + column)] =
                    grid.at(first_column - smooth + column, first_row - smooth + row) ? 1.0F
                                                                                     : 0.0F;
            }
        }
        const auto count = static_cast<float>(2 * smooth + 1);
        std::vector<float> across(static_cast<std::size_t>(columns * tall), 0.0F);
        for (std::int64_t row = 0; row < tall; row += 1) {
            for (std::int64_t column = 0; column < columns; column += 1) {
                float sum = 0.0F;
                for (std::int64_t k = 0; k <= 2 * smooth; k += 1) {
                    sum += raw[static_cast<std::size_t>(row * wide + column + k)];
                }
                across[static_cast<std::size_t>(row * columns + column)] = sum / count;
            }
        }
        for (std::int64_t row = 0; row < rows; row += 1) {
            for (std::int64_t column = 0; column < columns; column += 1) {
                float sum = 0.0F;
                for (std::int64_t k = 0; k <= 2 * smooth; k += 1) {
                    sum += across[static_cast<std::size_t>((row + k) * columns + column)];
                }
                values_[static_cast<std::size_t>(row * columns + column)] = sum / count;
            }
        }
    }

    [[nodiscard]] float at(std::int64_t column, std::int64_t row) const noexcept {
        const std::int64_t c = column - first_column_;
        const std::int64_t r = row - first_row_;
        if (c < 0 || r < 0 || c >= columns_ || r >= rows_) {
            return 0.0F;
        }
        return values_[static_cast<std::size_t>(r * columns_ + c)];
    }

private:
    std::int64_t first_column_;
    std::int64_t first_row_;
    std::int64_t columns_;
    std::int64_t rows_;
    std::vector<float> values_;
};

/// How solid the ground is around a point, in cells (the middle of cell i is at i + 0.5):
/// 0 in the open, 1 inside, smoothly in between.
float field(const Solid& solid, float u, float w) {
    const float fu = u - 0.5F;
    const float fw = w - 0.5F;
    const auto c = static_cast<std::int64_t>(std::floor(fu));
    const auto r = static_cast<std::int64_t>(std::floor(fw));
    const float tu = fu - static_cast<float>(c);
    const float tw = fw - static_cast<float>(r);
    const float top = solid.at(c, r) + (solid.at(c + 1, r) - solid.at(c, r)) * tu;
    const float bottom = solid.at(c, r + 1) + (solid.at(c + 1, r + 1) - solid.at(c, r + 1)) * tu;
    return top + (bottom - top) * tw;
}

/// A fixed speckle in [-1, 1] for a pixel of the world.
float speckle(std::int64_t x, std::int64_t y) {
    std::uint64_t value = static_cast<std::uint64_t>(x) * 0x9E3779B97F4A7C15ULL ^
                          static_cast<std::uint64_t>(y) * 0xC2B2AE3D27D4EB4FULL;
    value ^= value >> 29U;
    value *= 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 32U;
    return static_cast<float>(value & 0xFFFFU) / 32767.5F - 1.0F;
}

/// The cells a part's picture depends on: its own, one around it (and the ones it is smoothed
/// with), and those above it that its lit tops look up to.
std::vector<std::uint8_t> cells_seen(const Grid& grid, std::int64_t c0, std::int64_t r0,
                                     std::int64_t above, std::int64_t smooth) {
    std::vector<std::uint8_t> seen;
    for (std::int64_t row = r0 - above - 1 - smooth; row <= r0 + part_cells + smooth; row += 1) {
        for (std::int64_t column = c0 - 1 - smooth; column <= c0 + part_cells + smooth;
             column += 1) {
            seen.push_back(grid.at(column, row) ? 1U : 0U);
        }
    }
    return seen;
}

/// The colour of one pixel of the ground, from how deep under an open sky it is.
std::uint32_t ground_colour(const Look& look, float alpha, float depth, float grain) {
    const float top = depth < 1.0F ? std::pow(1.0F - depth, 1.6F) : 0.0F;
    const auto mixed = [&](std::uint32_t body, std::uint32_t lit) {
        const float value = static_cast<float>(channel(body)) +
                            (static_cast<float>(channel(lit)) - static_cast<float>(channel(body))) *
                                top +
                            grain * 40.0F * look.grain;
        return static_cast<std::uint32_t>(std::clamp(value, 0.0F, 255.0F));
    };
    const std::uint32_t red = mixed(look.red, look.top_red);
    const std::uint32_t green = mixed(look.green, look.top_green);
    const std::uint32_t blue = mixed(look.blue, look.top_blue);
    const auto a = static_cast<std::uint32_t>(std::clamp(alpha, 0.0F, 1.0F) * 255.0F + 0.5F);
    return (red << 24U) | (green << 16U) | (blue << 8U) | a;
}

/// The picture of the part whose top-left cell is (c0, r0), with one pixel more on each side
/// so that parts side by side join without a seam.
std::vector<std::uint32_t> paint(const Grid& grid, const Look& look, std::int64_t c0,
                                 std::int64_t r0, std::int64_t above, int side) {
    const auto pixels = static_cast<float>(std::max(look.pixels, 1U));
    const float lip_pixels = std::max(look.lip, 0.0F) / grid.cell * pixels;
    const Solid solid(grid, c0 - 1, r0 - above - 1, part_cells + 2, part_cells + above + 2,
                      static_cast<std::int64_t>(look.smooth));
    std::vector<std::uint32_t> out(static_cast<std::size_t>(side) * static_cast<std::size_t>(side),
                                   0U);
    for (int py = 0; py < side; py += 1) {
        const float w = static_cast<float>(r0) + (static_cast<float>(py) - 0.5F) / pixels;
        for (int px = 0; px < side; px += 1) {
            const float u = static_cast<float>(c0) + (static_cast<float>(px) - 0.5F) / pixels;
            const float value = field(solid, u, w);
            const float alpha = (value - 0.5F) * pixels + 0.5F;
            if (alpha <= 0.0F) {
                continue;
            }
            // How far up, in pixels, the open sky is (up to the lip).
            float depth = 1.0F;
            for (float up = 1.0F; up <= lip_pixels; up += 1.0F) {
                if (field(solid, u, w - up / pixels) < 0.5F) {
                    depth = up / std::max(lip_pixels, 1.0F);
                    break;
                }
            }
            const float grain = speckle(static_cast<std::int64_t>(std::floor(u * pixels)),
                                        static_cast<std::int64_t>(std::floor(w * pixels)));
            out[static_cast<std::size_t>(py) * static_cast<std::size_t>(side) +
                static_cast<std::size_t>(px)] = ground_colour(look, alpha, depth, grain);
        }
    }
    return out;
}

void remake(app::Renderer& renderer, Pictures& pictures, const Grid& grid, const Look& look,
            Drawn& drawn) {
    const auto pixels = static_cast<int>(std::max(look.pixels, 1U));
    const int side = static_cast<int>(part_cells) * pixels + 2;
    const auto above =
        static_cast<std::int64_t>(std::ceil(std::max(look.lip, 0.0F) / grid.cell)) + 1;
    for (std::int64_t pr = 0; pr < drawn.rows; pr += 1) {
        for (std::int64_t pc = 0; pc < drawn.columns; pc += 1) {
            Part& part = drawn.parts[static_cast<std::size_t>(pr * drawn.columns + pc)];
            const std::int64_t c0 = pc * part_cells;
            const std::int64_t r0 = pr * part_cells;
            std::vector<std::uint8_t> seen =
                cells_seen(grid, c0, r0, above, static_cast<std::int64_t>(look.smooth));
            if (part.image != 0 && seen == part.seen) {
                continue;
            }
            part.empty = !grid.any_in(grid.x + static_cast<float>(c0) * grid.cell,
                                      grid.y + static_cast<float>(r0) * grid.cell,
                                      grid.x + static_cast<float>(c0 + part_cells) * grid.cell,
                                      grid.y + static_cast<float>(r0 + part_cells) * grid.cell);
            if (part.image == 0) {
                part.image = pictures.next_image;
                pictures.next_image += 1;
            }
            part.seen = std::move(seen);
            if (!part.empty) {
                renderer.set_image(part.image, side, side,
                                   paint(grid, look, c0, r0, above, side), true);
            }
        }
    }
}

void forget_gone(app::Renderer& renderer, Pictures& pictures, const Cells& cells) {
    for (auto entry = pictures.drawn.begin(); entry != pictures.drawn.end();) {
        if (cells.grids.contains(entry->first)) {
            ++entry;
            continue;
        }
        for (const Part& part : entry->second.parts) {
            if (part.image != 0) {
                renderer.drop_image(part.image);
            }
        }
        entry = pictures.drawn.erase(entry);
    }
}

} // namespace

void draw(ecs::World& world) {
    const auto* backend = world.resource<app::RenderBackend>();
    const auto* cells = world.resource<Cells>();
    if (backend == nullptr || cells == nullptr || !backend->renderer().makes_images()) {
        return;
    }
    app::Renderer& renderer = backend->renderer();
    Pictures& pictures = pictures_of(world);
    forget_gone(renderer, pictures, *cells);
    auto& draws = sprite2d::layered_draws(world);
    world.each<const Terrain, const Look>([&](ecs::Entity entity, const Terrain& terrain,
                                              const Look& look) {
        const auto grid = cells->grids.find(entity.to_link());
        if (grid == cells->grids.end()) {
            return;
        }
        Drawn& drawn = pictures.drawn[entity.to_link()];
        if (drawn.parts.empty()) {
            drawn.columns = (static_cast<std::int64_t>(grid->second.columns) + part_cells - 1) /
                            part_cells;
            drawn.rows =
                (static_cast<std::int64_t>(grid->second.rows) + part_cells - 1) / part_cells;
            drawn.parts.resize(static_cast<std::size_t>(drawn.columns * drawn.rows));
        }
        if (drawn.revision != terrain.revision) {
            remake(renderer, pictures, grid->second, look, drawn);
            drawn.revision = terrain.revision;
        }
        if (world.has<transform2d::Hidden>(entity)) {
            return;
        }
        const float span = static_cast<float>(part_cells) * grid->second.cell;
        const float border = grid->second.cell / static_cast<float>(std::max(look.pixels, 1U));
        for (std::int64_t pr = 0; pr < drawn.rows; pr += 1) {
            for (std::int64_t pc = 0; pc < drawn.columns; pc += 1) {
                const Part& part = drawn.parts[static_cast<std::size_t>(pr * drawn.columns + pc)];
                if (part.empty || part.image == 0) {
                    continue;
                }
                app::DrawCommand command;
                command.what = "sprite";
                command.atlas = part.image;
                command.x = grid->second.x + (static_cast<float>(pc) + 0.5F) * span;
                command.y = grid->second.y + (static_cast<float>(pr) + 0.5F) * span;
                command.width = span + 2.0F * border;
                command.height = span + 2.0F * border;
                command.lit = !look.unlit;
                draws.add(look.layer, command);
            }
        }
    });
}

} // namespace ge::mod::terrain2d
