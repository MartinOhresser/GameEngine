#include "ge/app/time.hpp"
#include "ge/mod/collision2d/collision2d.gen.hpp"
#include "ge/mod/terrain2d/cells.hpp"
#include "ge/mod/terrain2d/terrain2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace ge::mod::terrain2d {

namespace {

Cells& cells_of(ecs::World& world) {
    if (auto* cells = world.resource<Cells>(); cells != nullptr) {
        return *cells;
    }
    return world.emplace_resource<Cells>();
}

/// A number in [-1, 1] for a whole coordinate and a seed: the same pair always gives the same.
float lattice(std::uint32_t seed, std::int64_t at) {
    std::uint64_t value = (static_cast<std::uint64_t>(seed) << 32U) ^
                          static_cast<std::uint64_t>(at) * 0x9E3779B97F4A7C15ULL;
    value ^= value >> 30U;
    value *= 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 27U;
    value *= 0x94D049BB133111EBULL;
    value ^= value >> 31U;
    return static_cast<float>(value >> 40U) / static_cast<float>(1U << 23U) - 1.0F;
}

/// Smooth noise along a line, in [-1, 1]; with period more than 0 it repeats every period.
float noise(std::uint32_t seed, float at, std::int64_t period) {
    const float base = std::floor(at);
    const float t = at - base;
    std::int64_t first = static_cast<std::int64_t>(base);
    std::int64_t second = first + 1;
    if (period > 0) {
        first = ((first % period) + period) % period;
        second = ((second % period) + period) % period;
    }
    const float smooth = t * t * (3.0F - 2.0F * t);
    return lattice(seed, first) + (lattice(seed, second) - lattice(seed, first)) * smooth;
}

/// Two layers of noise: broad hills with smaller bumps on them.
float hills(std::uint32_t seed, float at, std::int64_t period) {
    return 0.7F * noise(seed, at, period) +
           0.3F * noise(seed + 1U, at * 2.0F + 17.0F, period > 0 ? period * 2 : 0);
}

void generate_flat(const Terrain& terrain, Grid& grid) {
    const float wavelength = std::max(terrain.wavelength, 1.0F);
    for (std::uint32_t column = 0; column < grid.columns; column += 1) {
        const float top = terrain.surface +
                          terrain.amplitude *
                              hills(terrain.seed, static_cast<float>(column) / wavelength, 0);
        for (std::uint32_t row = 0; row < grid.rows; row += 1) {
            if (static_cast<float>(row) + 0.5F >= top) {
                grid.solid[grid.index(column, row)] = 1U;
            }
        }
    }
}

void generate_round(const Terrain& terrain, Grid& grid) {
    const float cx = static_cast<float>(grid.columns) / 2.0F;
    const float cy = static_cast<float>(grid.rows) / 2.0F;
    const float radius = std::min(cx, cy) - terrain.surface;
    const float around = 2.0F * std::numbers::pi_v<float> * std::max(radius, 1.0F);
    const auto period =
        std::max<std::int64_t>(static_cast<std::int64_t>(around / std::max(terrain.wavelength, 1.0F)), 3);
    for (std::uint32_t row = 0; row < grid.rows; row += 1) {
        for (std::uint32_t column = 0; column < grid.columns; column += 1) {
            const float dx = static_cast<float>(column) + 0.5F - cx;
            const float dy = static_cast<float>(row) + 0.5F - cy;
            const float turn = (std::atan2(dy, dx) / (2.0F * std::numbers::pi_v<float>) + 0.5F);
            const float edge =
                radius + terrain.amplitude *
                             hills(terrain.seed, turn * static_cast<float>(period), period);
            if (dx * dx + dy * dy <= edge * edge) {
                grid.solid[grid.index(column, row)] = 1U;
            }
        }
    }
}

std::uint32_t count_solid(const Grid& grid) {
    return static_cast<std::uint32_t>(std::ranges::count(grid.solid, std::uint8_t{1}));
}

void apply_shape(const Shape& shape, const transform2d::Position& at, Grid& grid) {
    const bool disc = shape.radius > 0.0F;
    const float half_w = disc ? shape.radius : shape.width / 2.0F;
    const float half_h = disc ? shape.radius : shape.height / 2.0F;
    const std::int64_t first_column = std::max<std::int64_t>(grid.column_of(at.x - half_w), 0);
    const std::int64_t last_column = std::min<std::int64_t>(
        grid.column_of(at.x + half_w), static_cast<std::int64_t>(grid.columns) - 1);
    const std::int64_t first_row = std::max<std::int64_t>(grid.row_of(at.y - half_h), 0);
    const std::int64_t last_row =
        std::min<std::int64_t>(grid.row_of(at.y + half_h), static_cast<std::int64_t>(grid.rows) - 1);
    for (std::int64_t row = first_row; row <= last_row; row += 1) {
        for (std::int64_t column = first_column; column <= last_column; column += 1) {
            const float mx = grid.x + (static_cast<float>(column) + 0.5F) * grid.cell - at.x;
            const float my = grid.y + (static_cast<float>(row) + 0.5F) * grid.cell - at.y;
            const bool in = disc ? mx * mx + my * my <= shape.radius * shape.radius
                                 : std::abs(mx) <= half_w && std::abs(my) <= half_h;
            if (in) {
                grid.solid[grid.index(column, row)] = shape.fill ? 1U : 0U;
            }
        }
    }
}

/// The terrains a body collides with: those whose layer its mask includes.
std::vector<const Grid*> grids_for(const Cells& cells, std::uint32_t mask) {
    std::vector<const Grid*> found;
    for (const auto& [link, grid] : cells.grids) {
        if ((grid.layer & mask) != 0U) {
            found.push_back(&grid);
        }
    }
    return found;
}

/// A body's box during the solving: its middle and half sizes.
struct Box {
    float x = 0.0F;
    float y = 0.0F;
    float half_w = 0.0F;
    float half_h = 0.0F;
};

bool free_at(const std::vector<const Grid*>& grids, const Box& box, float x, float y) {
    return std::ranges::none_of(grids, [&](const Grid* grid) {
        return grid->any_in(x - box.half_w, y - box.half_h, x + box.half_w, y + box.half_h);
    });
}

/// The smallest cell side of the grids: the steps of the solving are half of it.
float finest(const std::vector<const Grid*>& grids) {
    float side = grids.front()->cell;
    for (const Grid* grid : grids) {
        side = std::min(side, grid->cell);
    }
    return side;
}

/// What stopped a body during one tick.
struct Stops {
    bool below = false;
    bool above = false;
    bool left = false;
    bool right = false;
};

/// Moves the box along x by dx in small steps; low steps of ground are climbed while it stands.
void move_x(const std::vector<const Grid*>& grids, Box& box, float dx, float climb, Stops& stops) {
    const float side = finest(grids);
    const auto steps = static_cast<int>(std::ceil(std::abs(dx) / (side * 0.5F)));
    for (int step = 0; step < steps; step += 1) {
        const float next = box.x + dx / static_cast<float>(steps);
        if (free_at(grids, box, next, box.y)) {
            box.x = next;
            continue;
        }
        const bool standing = !free_at(grids, box, box.x, box.y + 1.0F);
        bool climbed = false;
        for (float lift = 1.0F; standing && lift <= climb; lift += 1.0F) {
            if (free_at(grids, box, next, box.y - lift)) {
                box.x = next;
                box.y -= lift;
                climbed = true;
                break;
            }
        }
        if (!climbed) {
            // Comes up against the wall, as close as a hundredth of a cell.
            float low = box.x;
            float high = next;
            for (int round = 0; round < 12; round += 1) {
                const float middle = (low + high) / 2.0F;
                (free_at(grids, box, middle, box.y) ? low : high) = middle;
            }
            box.x = low;
            (dx > 0.0F ? stops.right : stops.left) = true;
            return;
        }
    }
}

void move_y(const std::vector<const Grid*>& grids, Box& box, float dy, Stops& stops) {
    const float side = finest(grids);
    const auto steps = static_cast<int>(std::ceil(std::abs(dy) / (side * 0.5F)));
    for (int step = 0; step < steps; step += 1) {
        const float next = box.y + dy / static_cast<float>(steps);
        if (free_at(grids, box, box.x, next)) {
            box.y = next;
            continue;
        }
        float low = box.y;
        float high = next;
        for (int round = 0; round < 12; round += 1) {
            const float middle = (low + high) / 2.0F;
            (free_at(grids, box, box.x, middle) ? low : high) = middle;
        }
        box.y = low;
        (dy > 0.0F ? stops.below : stops.above) = true;
        return;
    }
}

/// Lifts a box that is inside the ground up to the first free place, a cell at a time.
bool lift_out(const std::vector<const Grid*>& grids, Box& box) {
    if (free_at(grids, box, box.x, box.y)) {
        return false;
    }
    const float side = finest(grids);
    for (int step = 1; step < 4096; step += 1) {
        const float y = box.y - side * static_cast<float>(step);
        if (free_at(grids, box, box.x, y)) {
            // Settles on the ground it came out of rather than a cell above it.
            float low = y;
            float high = y + side;
            for (int round = 0; round < 12; round += 1) {
                const float middle = (low + high) / 2.0F;
                (free_at(grids, box, box.x, middle) ? low : high) = middle;
            }
            box.y = low;
            return true;
        }
    }
    return false;
}

void note(collision2d::Blocked* blocked, const Stops& stops) {
    if (blocked == nullptr) {
        return;
    }
    blocked->below = blocked->below || stops.below;
    blocked->above = blocked->above || stops.above;
    blocked->left = blocked->left || stops.left;
    blocked->right = blocked->right || stops.right;
}

} // namespace

void build(ecs::World& world) {
    Cells& cells = cells_of(world);
    std::vector<std::uint64_t> alive;
    world.each<Terrain, const transform2d::Position>(
        [&](ecs::Entity entity, Terrain& terrain, const transform2d::Position& at) {
            const std::uint64_t link = entity.to_link();
            alive.push_back(link);
            if (terrain.generated && cells.grids.contains(link)) {
                return;
            }
            Grid grid;
            grid.columns = terrain.columns;
            grid.rows = terrain.rows;
            grid.cell = std::max(terrain.cell, 0.01F);
            grid.x = at.x;
            grid.y = at.y;
            grid.layer = terrain.layer;
            grid.solid.assign(static_cast<std::size_t>(grid.columns) * grid.rows, 0U);
            if (terrain.filled) {
                std::ranges::fill(grid.solid, std::uint8_t{1});
            } else if (terrain.round) {
                generate_round(terrain, grid);
            } else {
                generate_flat(terrain, grid);
            }
            cells.grids[link] = std::move(grid);
            terrain.generated = true;
            terrain.revision += 1U;
        });
    std::erase_if(cells.grids, [&alive](const auto& entry) {
        return std::ranges::find(alive, entry.first) == alive.end();
    });

    world.each<Shape, const transform2d::Position>(
        [&](ecs::Entity, Shape& shape, const transform2d::Position& at) {
            if (shape.applied) {
                return;
            }
            for (auto& [link, grid] : cells.grids) {
                if ((grid.layer & shape.layers) != 0U) {
                    apply_shape(shape, at, grid);
                    if (auto* terrain = world.get<Terrain>(ecs::Entity::from_link(link));
                        terrain != nullptr) {
                        terrain->revision += 1U;
                    }
                }
            }
            shape.applied = true;
        });
    world.each<Terrain>([&](ecs::Entity entity, Terrain& terrain) {
        if (const auto found = cells.grids.find(entity.to_link()); found != cells.grids.end()) {
            terrain.solid_cells = count_solid(found->second);
        }
    });
}

void dig(ecs::World& world) {
    Cells& cells = cells_of(world);
    world.each<Digger, const transform2d::Position>(
        [&](ecs::Entity entity, Digger& digger, const transform2d::Position& at) {
            digger.last = 0U;
            if (world.has<transform2d::Disabled>(entity)) {
                return;
            }
            for (auto& [link, grid] : cells.grids) {
                if ((grid.layer & digger.layers) == 0U) {
                    continue;
                }
                const std::uint32_t carved = grid.carve(at.x, at.y, digger.radius);
                if (carved == 0U) {
                    continue;
                }
                digger.last += carved;
                if (auto* terrain = world.get<Terrain>(ecs::Entity::from_link(link));
                    terrain != nullptr) {
                    terrain->solid_cells -= std::min(terrain->solid_cells, carved);
                    terrain->revision += 1U;
                }
            }
            digger.removed += digger.last;
        });
}

void collide(ecs::World& world) {
    const Cells& cells = cells_of(world);
    if (cells.grids.empty()) {
        return;
    }
    const float tick = world.resource<app::Time>()->fixed_delta *
                       world.resource<transform2d::Params>()->speed_scale;
    world.each<const collision2d::Body, const collision2d::Collider, transform2d::Position,
               transform2d::Velocity>([&](ecs::Entity entity, const collision2d::Body&,
                                          const collision2d::Collider& collider,
                                          transform2d::Position& position,
                                          transform2d::Velocity& velocity) {
        const std::vector<const Grid*> grids = grids_for(cells, collider.mask);
        if (grids.empty()) {
            return;
        }
        float climb = 0.0F;
        world.each<const Terrain>([&](ecs::Entity, const Terrain& terrain) {
            if ((terrain.layer & collider.mask) != 0U) {
                climb = std::max(climb, terrain.climb);
            }
        });
        const float scale = world.has<transform2d::SpeedScale>(entity)
                                ? world.get<transform2d::SpeedScale>(entity)->factor
                                : 1.0F;
        const float dx = velocity.x * tick * scale;
        const float dy = velocity.y * tick * scale;
        Box box{position.x - dx, position.y - dy, collider.width / 2.0F, collider.height / 2.0F};
        Stops stops;
        stops.below = lift_out(grids, box);
        move_x(grids, box, dx, climb, stops);
        move_y(grids, box, dy, stops);
        if (stops.below && velocity.y > 0.0F) {
            velocity.y = 0.0F;
        }
        if (stops.above && velocity.y < 0.0F) {
            velocity.y = 0.0F;
        }
        if ((stops.left && velocity.x < 0.0F) || (stops.right && velocity.x > 0.0F)) {
            velocity.x = 0.0F;
        }
        position.x = box.x;
        position.y = box.y;
        note(world.get<collision2d::Blocked>(entity), stops);
    });
}

} // namespace ge::mod::terrain2d
