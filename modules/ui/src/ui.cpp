#include "ge/app/fields.hpp"
#include "ge/app/input.hpp"
#include "ge/app/lists.hpp"
#include "ge/app/runtime_lists.hpp"
#include "ge/app/scene.hpp"
#include "ge/app/viewport.hpp"
#include "ge/mod/sprite2d/sprite2d.gen.hpp"
#include "ge/mod/tag/find.hpp"
#include "ge/mod/text2d/text2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"
#include "ge/mod/ui/ui.gen.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <optional>
#include <utility>
#include <vector>

namespace ge::mod::ui {

namespace {

/// The name of an action of the game, or null when the index is not one.
const std::string* action_name(const app::ActionNames* names, std::uint32_t action) {
    if (names == nullptr || action >= names->names.size()) {
        return nullptr;
    }
    return &names->names[action];
}

bool pressed(const app::Actions& actions, const app::ActionNames* names, std::uint32_t action) {
    const std::string* name = action_name(names, action);
    return name != nullptr && actions.pressed(*name);
}

float value_of(const app::Actions& actions, const app::ActionNames* names, std::uint32_t action) {
    const std::string* name = action_name(names, action);
    return name == nullptr ? 0.0F : actions.value(*name);
}

/// A button of a menu, gathered so the menu can move the focus among them.
struct Candidate {
    ecs::Entity entity;
    std::uint32_t order = 0;
    std::uint32_t action = 0;
    float x = 0.0F;
    float y = 0.0F;
    float half_width = 0.0F;
    float half_height = 0.0F;
};

std::vector<Candidate> buttons_of(ecs::World& world, std::uint32_t menu) {
    std::vector<Candidate> found;
    world.each<const Button, const transform2d::Position>(
        [&](ecs::Entity entity, const Button& button, const transform2d::Position& position) {
            if (button.menu == menu && button.enabled && !world.has<transform2d::Hidden>(entity)) {
                found.push_back(Candidate{entity, button.order, button.action, position.x,
                                          position.y, button.width / 2.0F, button.height / 2.0F});
            }
        });
    std::stable_sort(found.begin(), found.end(),
                     [](const Candidate& a, const Candidate& b) { return a.order < b.order; });
    return found;
}

enum class Direction : std::uint8_t { Up, Down, Left, Right };

/// The buttons as a table: the ones that sit side by side (their boxes overlap vertically, and
/// they are not on top of each other) share a row; rows go from top to bottom, the buttons of a
/// row from left to right. Each cell is an index into `buttons`. Buttons that have the same place
/// count as different rows, one after the other in the order of their numbers.
std::vector<std::vector<std::size_t>> rows_of(const std::vector<Candidate>& buttons) {
    std::vector<std::size_t> by_height(buttons.size());
    std::iota(by_height.begin(), by_height.end(), std::size_t{0});
    std::stable_sort(by_height.begin(), by_height.end(), [&buttons](std::size_t a, std::size_t b) {
        return buttons[a].y < buttons[b].y;
    });
    std::vector<std::vector<std::size_t>> rows;
    for (const std::size_t index : by_height) {
        const Candidate& button = buttons[index];
        bool beside = !rows.empty();
        if (beside) {
            const Candidate& first = buttons[rows.back().front()];
            beside = std::fabs(button.y - first.y) < first.half_height + button.half_height &&
                     std::ranges::none_of(rows.back(), [&](std::size_t member) {
                         return std::fabs(buttons[member].x - button.x) < 1.0F;
                     });
        }
        if (beside) {
            rows.back().push_back(index);
        } else {
            rows.push_back({index});
        }
    }
    for (std::vector<std::size_t>& row : rows) {
        std::stable_sort(row.begin(), row.end(), [&buttons](std::size_t a, std::size_t b) {
            return buttons[a].x < buttons[b].x;
        });
    }
    return rows;
}

/// The position `step` (-1 or 1) from `at` in a line of `count` places: round the ends with
/// `wrap`, else where it already is.
std::size_t stepped(std::size_t at, std::size_t count, int step, bool wrap) {
    if (step < 0) {
        if (at > 0) {
            return at - 1;
        }
        return wrap ? count - 1 : at;
    }
    if (at + 1 < count) {
        return at + 1;
    }
    return wrap ? 0 : at;
}

/// Where the focus goes after a key: left and right move along the row, up and down go to the
/// row above or below, to the button of it that is the nearest to where the focus is. An index
/// among the sorted buttons (the same one when there is nowhere to go).
std::size_t moved_focus(const std::vector<Candidate>& buttons, std::size_t current,
                        Direction direction, bool wrap) {
    const std::vector<std::vector<std::size_t>> rows = rows_of(buttons);
    std::size_t row = 0;
    std::size_t column = 0;
    for (std::size_t r = 0; r < rows.size(); ++r) {
        const auto found = std::ranges::find(rows[r], current);
        if (found != rows[r].end()) {
            row = r;
            column = static_cast<std::size_t>(found - rows[r].begin());
        }
    }
    if (direction == Direction::Left || direction == Direction::Right) {
        const int step = direction == Direction::Left ? -1 : 1;
        return rows[row][stepped(column, rows[row].size(), step, wrap)];
    }
    const int step = direction == Direction::Up ? -1 : 1;
    const std::size_t target = stepped(row, rows.size(), step, wrap);
    if (target == row) {
        return current;
    }
    const float x = buttons[current].x;
    return *std::ranges::min_element(rows[target], [&](std::size_t a, std::size_t b) {
        return std::fabs(buttons[a].x - x) < std::fabs(buttons[b].x - x);
    });
}

std::optional<std::size_t> hovered(const std::vector<Candidate>& buttons, float x, float y) {
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        const Candidate& b = buttons[i];
        if (std::fabs(x - b.x) <= b.half_width && std::fabs(y - b.y) <= b.half_height) {
            return i;
        }
    }
    return std::nullopt;
}

void raise(app::Actions& actions, const app::ActionNames* names, std::uint32_t action) {
    if (const std::string* name = action_name(names, action); name != nullptr) {
        actions.raise(*name);
    }
}

/// One menu's tick: keyboard focus, pointer focus, and the press.
void run_menu(ecs::World& world, Menu& menu, app::Actions& actions, const app::ActionNames* names) {
    const std::vector<Candidate> buttons = buttons_of(world, menu.group);
    if (buttons.empty()) {
        return;
    }
    const auto by_order = std::find_if(buttons.begin(), buttons.end(), [&menu](const Candidate& c) {
        return c.order == menu.focused;
    });
    std::size_t current =
        by_order == buttons.end() ? 0U : static_cast<std::size_t>(by_order - buttons.begin());
    const std::array<std::pair<std::uint32_t, Direction>, 4> keys = {
        {{menu.up, Direction::Up},
         {menu.down, Direction::Down},
         {menu.left, Direction::Left},
         {menu.right, Direction::Right}}};
    for (const auto& [action, direction] : keys) {
        if (pressed(actions, names, action)) {
            current = moved_focus(buttons, current, direction, menu.wrap);
        }
    }
    bool clicked = false;
    if (menu.use_pointer) {
        const float x = value_of(actions, names, menu.pointer_x);
        const float y = value_of(actions, names, menu.pointer_y);
        const std::optional<std::size_t> over = hovered(buttons, x, y);
        // The pointer takes the focus only when it moves, so it does not undo the keyboard.
        if (over && (x != menu.last_x || y != menu.last_y)) {
            current = *over;
        }
        clicked = over && pressed(actions, names, menu.click);
        if (clicked) {
            current = *over;
        }
        menu.last_x = x;
        menu.last_y = y;
    }
    menu.focused = buttons[current].order;
    if (clicked || pressed(actions, names, menu.accept)) {
        raise(actions, names, buttons[current].action);
    }
}

} // namespace

void anchor(ecs::World& world) {
    const auto* view = world.resource<app::Viewport>();
    if (view == nullptr) {
        return;
    }
    const auto width = static_cast<float>(view->width);
    const auto height = static_cast<float>(view->height);
    world.each<const Anchor, transform2d::Position>(
        [width, height](ecs::Entity, const Anchor& at, transform2d::Position& position) {
            position.x = width * at.anchor_x + at.offset_x;
            position.y = height * at.anchor_y + at.offset_y;
        });
}

void layout(ecs::World& world) {
    struct Placed {
        Grid grid;
        transform2d::Position origin;
    };
    std::vector<Placed> grids;
    world.each<const Grid, const transform2d::Position>(
        [&grids](ecs::Entity, const Grid& grid, const transform2d::Position& position) {
            grids.push_back(Placed{grid, position});
        });
    if (grids.empty()) {
        return;
    }
    world.each<const Cell, transform2d::Position>(
        [&grids](ecs::Entity, const Cell& cell, transform2d::Position& position) {
            const auto grid = std::find_if(grids.begin(), grids.end(), [&cell](const Placed& p) {
                return p.grid.group == cell.group;
            });
            if (grid == grids.end()) {
                return;
            }
            const std::uint32_t columns = std::max<std::uint32_t>(grid->grid.columns, 1U);
            const auto column = static_cast<float>(cell.index % columns);
            const auto row = static_cast<float>(static_cast<std::uint32_t>(cell.index / columns));
            position.x = grid->origin.x + column * (grid->grid.cell_width + grid->grid.spacing_x);
            position.y = grid->origin.y + row * (grid->grid.cell_height + grid->grid.spacing_y);
        });
}

void bind(ecs::World& world) {
    const auto* table = world.resource<app::FieldTable>();
    if (table == nullptr) {
        return;
    }
    // The oldest entity of each tag is the one a Bind finds.
    const tag::Tagged tagged = tag::tagged(world);
    world.each<const Bind>([&](ecs::Entity entity, const Bind& binding) {
        float value = 0.0F;
        const ecs::Entity from = binding.own ? entity : tagged.find(binding.source);
        if (!from.is_null() && binding.field < table->entries.size()) {
            const app::FieldEntry& field = table->entries[binding.field];
            if (field.component != nullptr && field.component->read != nullptr) {
                if (const auto read = field.component->read(world, from, field.field)) {
                    value = app::scalar_as<float>(*read);
                }
            }
        }
        if (auto* text = world.get<text2d::Text>(entity); text != nullptr) {
            text->value = value;
        }
        if (auto* bar = world.get<Bar>(entity); bar != nullptr) {
            bar->value = value;
        }
    });
}

void fill(ecs::World& world) {
    world.each<const Bar, sprite2d::Sprite>(
        [](ecs::Entity, const Bar& bar, sprite2d::Sprite& sprite) {
            const float ratio = bar.max > 0.0F ? std::clamp(bar.value / bar.max, 0.0F, 1.0F) : 0.0F;
            // A sprite of size 0 would be drawn at its picture's own size, so an empty bar is
            // made vanishingly thin instead.
            const float along = std::max(bar.length * ratio, 0.001F);
            sprite.width = bar.vertical ? bar.thickness : along;
            sprite.height = bar.vertical ? along : bar.thickness;
        });
}

void menu(ecs::World& world) {
    auto* actions = world.resource<app::Actions>();
    const auto* names = world.resource<app::ActionNames>();
    if (actions == nullptr) {
        return;
    }
    world.each<Menu>(
        [&](ecs::Entity, Menu& controller) { run_menu(world, controller, *actions, names); });
    // Show which button has the focus.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> focus; // menu, focused order
    world.each<const Menu>([&focus](ecs::Entity, const Menu& controller) {
        focus.emplace_back(controller.group, controller.focused);
    });
    world.each<const Button, sprite2d::Sprite>(
        [&focus](ecs::Entity, const Button& button, sprite2d::Sprite& sprite) {
            const bool focused = std::any_of(focus.begin(), focus.end(), [&button](const auto& f) {
                return f.first == button.menu && f.second == button.order;
            });
            if (!button.enabled) {
                sprite.frame = button.disabled_frame;
            } else {
                sprite.frame = focused ? button.focus_frame : button.normal_frame;
            }
        });
}

namespace {

/// The name of an item of the game's "runtime_list" list, empty when it is not one.
std::string_view list_name(const app::ListNames* names, std::uint32_t list) {
    return names == nullptr ? std::string_view{} : names->name("runtime_list", list);
}

std::uint32_t pages_of(std::size_t count, std::uint32_t page_size) {
    const std::size_t size = std::max<std::uint32_t>(page_size, 1U);
    return static_cast<std::uint32_t>(std::max<std::size_t>((count + size - 1U) / size, 1U));
}

/// One Pager's tick: the buttons Previous and Next turn the page, kept inside the list.
void turn_page(Pager& pager, std::size_t count, const app::Actions& actions,
               const app::ActionNames* names) {
    pager.pages = pages_of(count, pager.page_size);
    if (pressed(actions, names, pager.previous) && pager.page > 0) {
        --pager.page;
    }
    if (pressed(actions, names, pager.next)) {
        ++pager.page;
    }
    pager.page = std::min(pager.page, pager.pages - 1U);
}

} // namespace

void pager(ecs::World& world) {
    const auto* lists = world.resource<app::RuntimeLists>();
    const auto* list_names = world.resource<app::ListNames>();
    const auto* actions = world.resource<app::Actions>();
    if (lists == nullptr || actions == nullptr) {
        return;
    }
    const auto* names = world.resource<app::ActionNames>();
    world.each<Pager>([&](ecs::Entity, Pager& pager) {
        turn_page(pager, lists->size(list_name(list_names, pager.list)), *actions, names);
    });
    std::vector<Pager> pagers;
    world.each<const Pager>(
        [&pagers](ecs::Entity, const Pager& pager) { pagers.push_back(pager); });
    world.each<Row>([&](ecs::Entity entity, Row& row) {
        const auto owner = std::find_if(pagers.begin(), pagers.end(),
                                        [&row](const Pager& p) { return p.list == row.list; });
        if (owner == pagers.end()) {
            return;
        }
        row.item = owner->page * std::max<std::uint32_t>(owner->page_size, 1U) + row.row;
        if (auto* dynamic = world.get<text2d::Dynamic>(entity); dynamic != nullptr) {
            dynamic->list = row.list;
            dynamic->index = row.item;
        }
        if (auto* button = world.get<Button>(entity); button != nullptr) {
            button->enabled = row.item < lists->size(list_name(list_names, row.list));
        }
    });
    world.each<RowButton>([&](ecs::Entity entity, RowButton& row) {
        const auto owner = std::find_if(pagers.begin(), pagers.end(),
                                        [&row](const Pager& p) { return p.list == row.list; });
        if (owner == pagers.end()) {
            return;
        }
        row.item = owner->page * std::max<std::uint32_t>(owner->page_size, 1U) + row.row;
        if (auto* button = world.get<Button>(entity); button != nullptr) {
            button->enabled = row.item < lists->size(list_name(list_names, row.list));
        }
    });
}

void select(ecs::World& world) {
    auto* lists = world.resource<app::RuntimeLists>();
    const auto* list_names = world.resource<app::ListNames>();
    if (lists == nullptr) {
        return;
    }
    std::vector<std::pair<std::uint32_t, std::uint32_t>> focus; // menu group, focused order
    world.each<const Menu>([&focus](ecs::Entity, const Menu& controller) {
        focus.emplace_back(controller.group, controller.focused);
    });
    std::vector<Pager> pagers;
    world.each<const Pager>(
        [&pagers](ecs::Entity, const Pager& pager) { pagers.push_back(pager); });
    // A Row and a RowButton stand for an item the same way: when its button has the focus, the
    // item is the selected one of the list.
    const auto choose = [&](std::uint32_t list, std::uint32_t item, const Button& button) {
        const bool focused =
            button.enabled && std::any_of(focus.begin(), focus.end(), [&button](const auto& f) {
                return f.first == button.menu && f.second == button.order;
            });
        const auto owner = std::find_if(pagers.begin(), pagers.end(), [&](const Pager& p) {
            return p.list == list && p.menu == button.menu;
        });
        if (!focused || owner == pagers.end()) {
            return;
        }
        if (const std::string_view name = list_name(list_names, list); !name.empty()) {
            lists->select(name, item);
        }
    };
    world.each<const Row, const Button>([&](ecs::Entity, const Row& row, const Button& button) {
        choose(row.list, row.item, button);
    });
    world.each<const RowButton, const Button>(
        [&](ecs::Entity, const RowButton& row, const Button& button) {
            choose(row.list, row.item, button);
        });
}

} // namespace ge::mod::ui
