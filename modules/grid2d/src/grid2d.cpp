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
    const auto* viewport = world.resource<app::Viewport>();
    // The oldest camera: where the window's centre is in the world, and how enlarged.
    float camera_x = 0.0F;
    float camera_y = 0.0F;
    float zoom = 1.0F;
    bool has_camera = false;
    world.each<const camera2d::Camera, const transform2d::Position>(
        [&](ecs::Entity, const camera2d::Camera& camera, const transform2d::Position& position) {
            if (!has_camera) {
                has_camera = true;
                camera_x = position.x;
                camera_y = position.y;
                zoom = camera.zoom > 0.0F ? camera.zoom : 1.0F;
            }
        });
    const Boards boards(world);
    world.each<GridCursor>([&](ecs::Entity entity, GridCursor& cursor) {
        cursor.clicked = false;
        cursor.over = false;
        const Layout* layout = boards.find(cursor.grid);
        if (layout == nullptr || world.has<transform2d::Disabled>(entity)) {
            return;
        }
        float x = value_of(actions, names, cursor.pointer_x);
        float y = value_of(actions, names, cursor.pointer_y);
        if (has_camera && viewport != nullptr) {
            x = (x - static_cast<float>(viewport->width) / 2.0F) / zoom + camera_x;
            y = (y - static_cast<float>(viewport->height) / 2.0F) / zoom + camera_y;
        }
        const std::int32_t col = layout->col_at(x);
        const std::int32_t row = layout->row_at(y);
        if (!layout->contains(col, row)) {
            return;
        }
        cursor.over = true;
        cursor.col = col;
        cursor.row = row;
        if (pressed(actions, names, cursor.click)) {
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

} // namespace ge::mod::grid2d
