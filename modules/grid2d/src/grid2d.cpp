#include "ge/app/fields.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/app/spawn.hpp"
#include "ge/app/viewport.hpp"
#include "ge/core/random.hpp"
#include "ge/mod/camera2d/camera2d.gen.hpp"
#include "ge/mod/grid2d/cells.hpp"
#include "ge/mod/grid2d/grid2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace ge::mod::grid2d {

namespace {

/// The value of an action of the game, 0 when it is none or unknown.
float value_of(const app::Actions* actions, const app::ActionNames* names, std::uint32_t action) {
    if (actions == nullptr || names == nullptr || action >= names->names.size()) {
        return 0.0F;
    }
    return actions->value(names->names[action]);
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

/// The layouts of the boards, found once per system run.
class Boards {
public:
    explicit Boards(ecs::World& world) {
        world.each<const Grid>([&](ecs::Entity, const Grid& grid) {
            const bool known = std::ranges::any_of(
                layouts_, [&grid](const Layout& layout) { return layout.id == grid.id; });
            if (!known) {
                if (const auto layout = layout_of(world, grid.id)) {
                    layouts_.push_back(*layout);
                }
            }
        });
    }

    [[nodiscard]] const Layout* find(std::uint32_t id) const {
        const auto found =
            std::ranges::find_if(layouts_, [id](const Layout& layout) { return layout.id == id; });
        return found == layouts_.end() ? nullptr : &*found;
    }

private:
    std::vector<Layout> layouts_;
};

/// Whether the entity counts for a zone: it has the number the zone looks for (any entity does
/// when the zone looks for none).
bool matches(const ecs::World& world, ecs::Entity entity, const app::FieldEntry* field,
             float value) {
    if (field == nullptr) {
        return true;
    }
    if (field->component->read == nullptr) {
        return false;
    }
    const auto read = field->component->read(world, entity, field->field);
    return read && app::scalar_as<float>(*read) == value;
}

const app::FieldEntry* field_of(const app::FieldTable* table, std::uint32_t index) {
    if (table == nullptr || index >= table->entries.size() ||
        table->entries[index].component == nullptr) {
        return nullptr;
    }
    return &table->entries[index];
}

bool in_zone(const Zone& zone, const GridPos& at) {
    return at.grid == zone.grid && at.col >= zone.col && at.row >= zone.row &&
           at.col < zone.col + static_cast<std::int32_t>(zone.columns) &&
           at.row < zone.row + static_cast<std::int32_t>(zone.rows);
}

/// Where the window's centre is in the world, and how enlarged: the oldest camera.
struct View {
    bool has_camera = false;
    float x = 0.0F;
    float y = 0.0F;
    float zoom = 1.0F;
    float width = 0.0F;
    float height = 0.0F;

    explicit View(ecs::World& world) {
        if (const auto* viewport = world.resource<app::Viewport>(); viewport != nullptr) {
            width = static_cast<float>(viewport->width);
            height = static_cast<float>(viewport->height);
        }
        world.each<const camera2d::Camera, const transform2d::Position>(
            [this](ecs::Entity, const camera2d::Camera& camera,
                   const transform2d::Position& position) {
                if (!has_camera) {
                    has_camera = true;
                    x = position.x;
                    y = position.y;
                    zoom = camera.zoom > 0.0F ? camera.zoom : 1.0F;
                }
            });
        has_camera = has_camera && width > 0.0F && height > 0.0F;
    }
};

/// One step of a cursor along an axis: it goes round with wrap, otherwise it stops at the edge.
std::int32_t step(std::int32_t at, std::int32_t delta, std::int32_t count, bool wrap) {
    const std::int32_t next = at + delta;
    if (wrap) {
        return ((next % count) + count) % count;
    }
    return std::clamp(next, 0, count - 1);
}

/// Moves the cursor with its keys; true when a key moved it.
bool steer_with_keys(GridCursor& cursor, const Layout& layout, const app::Actions* actions,
                     const app::ActionNames* names) {
    const std::int32_t dx = (pressed(actions, names, cursor.right) ? 1 : 0) -
                            (pressed(actions, names, cursor.left) ? 1 : 0);
    const std::int32_t dy = (pressed(actions, names, cursor.down) ? 1 : 0) -
                            (pressed(actions, names, cursor.up) ? 1 : 0);
    if (dx == 0 && dy == 0) {
        return false;
    }
    const std::int32_t col = std::clamp(cursor.col, 0, layout.columns - 1);
    const std::int32_t row = std::clamp(cursor.row, 0, layout.rows - 1);
    cursor.col = step(col, dx, layout.columns, cursor.wrap);
    cursor.row = step(row, dy, layout.rows, cursor.wrap);
    return true;
}

/// Puts the cursor on the cell under the pointer (through the camera for a board in the
/// world); over is false when the pointer is on no cell.
void follow_pointer(GridCursor& cursor, const Layout& layout, const View& view, float x, float y) {
    if (!layout.screen && view.has_camera) {
        x = (x - view.width / 2.0F) / view.zoom + view.x;
        y = (y - view.height / 2.0F) / view.zoom + view.y;
    }
    const std::int32_t col = layout.col_at(x);
    const std::int32_t row = layout.row_at(y);
    cursor.over = layout.contains(col, row);
    if (cursor.over) {
        cursor.col = col;
        cursor.row = row;
    }
}

} // namespace

void place(ecs::World& world) {
    const Boards boards(world);
    world.each<GridPos, transform2d::Position>(
        [&](ecs::Entity, GridPos& at, transform2d::Position& position) {
            const Layout* layout = boards.find(at.grid);
            if (layout == nullptr) {
                return;
            }
            if (at.from_position) {
                at.col = layout->col_at(position.x);
                at.row = layout->row_at(position.y);
                at.from_position = false;
            }
            if (!layout->contains(at.col, at.row)) {
                return;
            }
            const float goal_x = layout->x_of(at.col);
            const float goal_y = layout->y_of(at.row);
            const float share = std::clamp(at.glide, 0.0F, 1.0F);
            const float left_x = goal_x - position.x;
            const float left_y = goal_y - position.y;
            if (share <= 0.0F || (std::abs(left_x) < 1.0F && std::abs(left_y) < 1.0F)) {
                position.x = goal_x;
                position.y = goal_y;
            } else {
                position.x += left_x * share;
                position.y += left_y * share;
            }
        });
}

void point(ecs::World& world) {
    const auto* actions = world.resource<app::Actions>();
    auto* raising = world.resource<app::Actions>();
    const auto* names = world.resource<app::ActionNames>();
    const View view(world);
    const Boards boards(world);
    world.each<GridCursor>([&](ecs::Entity entity, GridCursor& cursor) {
        cursor.clicked = false;
        const Layout* layout = boards.find(cursor.grid);
        if (layout == nullptr || world.has<transform2d::Disabled>(entity)) {
            cursor.over = false;
            return;
        }
        const float x = value_of(actions, names, cursor.pointer_x);
        const float y = value_of(actions, names, cursor.pointer_y);
        const bool pointer_moved = x != cursor.last_x || y != cursor.last_y;
        cursor.last_x = x;
        cursor.last_y = y;
        if (steer_with_keys(cursor, *layout, actions, names)) {
            cursor.keyed = true;
        } else if (pointer_moved) {
            cursor.keyed = false;
        }
        if (cursor.keyed) {
            cursor.col = std::clamp(cursor.col, 0, layout->columns - 1);
            cursor.row = std::clamp(cursor.row, 0, layout->rows - 1);
            cursor.over = true;
        } else {
            follow_pointer(cursor, *layout, view, x, y);
        }
        const bool clicked = (!cursor.keyed && pressed(actions, names, cursor.click)) ||
                             pressed(actions, names, cursor.accept);
        if (cursor.over && clicked) {
            cursor.clicked = true;
            raise(raising, names, cursor.action);
        }
    });
}

void spawn(ecs::World& world) {
    const auto* actions = world.resource<app::Actions>();
    const auto* names = world.resource<app::ActionNames>();
    auto* requests = world.resource<app::SpawnRequests>();
    auto* random = world.resource<Random>();
    if (requests == nullptr || random == nullptr) {
        return;
    }
    // Collected first: a spawn changes nothing in the world until the next tick, but the
    // spawners of one tick must not choose the same cell.
    std::vector<std::pair<std::uint32_t, std::vector<std::size_t>>> taken;
    world.each<const SpawnOnEmpty>([&](ecs::Entity entity, const SpawnOnEmpty& spawner) {
        if (!pressed(actions, names, spawner.trigger) || world.has<transform2d::Disabled>(entity)) {
            return;
        }
        const auto layout = layout_of(world, spawner.grid);
        if (!layout) {
            return;
        }
        const std::vector<ecs::Entity> cells = occupancy(world, *layout);
        std::vector<std::size_t> empty;
        for (std::size_t i = 0; i < cells.size(); ++i) {
            const bool taken_now = std::ranges::any_of(taken, [&](const auto& item) {
                return item.first == spawner.grid &&
                       std::ranges::find(item.second, i) != item.second.end();
            });
            if (cells[i].is_null() && !taken_now) {
                empty.push_back(i);
            }
        }
        std::vector<std::size_t> chosen;
        for (std::uint32_t n = 0; n < spawner.count && !empty.empty(); ++n) {
            const auto pick =
                static_cast<std::size_t>(random->below(static_cast<std::uint32_t>(empty.size())));
            const std::size_t cell = empty[pick];
            empty.erase(empty.begin() + static_cast<std::ptrdiff_t>(pick));
            chosen.push_back(cell);
            const bool alternative =
                spawner.alt_percent > 0U && random->below(100U) < spawner.alt_percent;
            const auto col =
                static_cast<std::int32_t>(cell % static_cast<std::size_t>(layout->columns));
            const auto row =
                static_cast<std::int32_t>(cell / static_cast<std::size_t>(layout->columns));
            requests->spawn(alternative ? spawner.alt_prefab : spawner.prefab, layout->x_of(col),
                            layout->y_of(row));
        }
        taken.emplace_back(spawner.grid, std::move(chosen));
    });
}

void watch_zones(ecs::World& world) {
    const auto* table = world.resource<app::FieldTable>();
    const auto* names = world.resource<app::ActionNames>();
    auto* actions = world.resource<app::Actions>();
    std::vector<ecs::Entity> zones;
    world.each<const Zone>([&](ecs::Entity entity, const Zone&) { zones.push_back(entity); });
    for (const ecs::Entity entity : zones) {
        auto* zone = world.get<Zone>(entity);
        if (zone == nullptr || world.has<transform2d::Disabled>(entity)) {
            continue;
        }
        const app::FieldEntry* field = field_of(table, zone->match_field);
        std::uint32_t count = 0;
        ecs::Entity first;
        world.each<const GridPos>([&](ecs::Entity other, const GridPos& at) {
            if (in_zone(*zone, at) && !world.has<transform2d::Disabled>(other) &&
                matches(world, other, field, zone->match_value)) {
                if (count == 0U) {
                    first = other;
                }
                ++count;
            }
        });
        const bool more = count > zone->count;
        zone->count = count;
        zone->entered = count == 0U ? 0U : first.to_link();
        if (more) {
            raise(actions, names, zone->action);
        }
    }
}

} // namespace ge::mod::grid2d
