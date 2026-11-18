#include "ge/app/fields.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/app/viewport.hpp"
#include "ge/mod/sprite2d/sprite2d.gen.hpp"
#include "ge/mod/text2d/text2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"
#include "ge/mod/ui/ui.gen.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
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
            if (button.menu == menu) {
                found.push_back(Candidate{entity, button.order, button.action, position.x,
                                          position.y, button.width / 2.0F, button.height / 2.0F});
            }
        });
    std::stable_sort(found.begin(), found.end(),
                     [](const Candidate& a, const Candidate& b) { return a.order < b.order; });
    return found;
}

/// Where the focus goes after `up` or `down`, an index among the sorted buttons.
std::size_t moved_focus(std::size_t current, std::size_t count, int step, bool wrap) {
    const std::size_t last = count - 1;
    if (step < 0) {
        if (current > 0) {
            return current - 1;
        }
        return wrap ? last : 0;
    }
    if (current < last) {
        return current + 1;
    }
    return wrap ? 0 : last;
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
    if (pressed(actions, names, menu.up)) {
        current = moved_focus(current, buttons.size(), -1, menu.wrap);
    }
    if (pressed(actions, names, menu.down)) {
        current = moved_focus(current, buttons.size(), 1, menu.wrap);
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
    std::vector<std::pair<std::uint32_t, ecs::Entity>> tagged;
    world.each<const Tag>([&tagged](ecs::Entity entity, const Tag& tag) {
        const bool known = std::any_of(tagged.begin(), tagged.end(),
                                       [&tag](const auto& item) { return item.first == tag.tag; });
        if (!known) {
            tagged.emplace_back(tag.tag, entity);
        }
    });
    world.each<const Bind>([&](ecs::Entity entity, const Bind& binding) {
        float value = 0.0F;
        const auto source =
            std::find_if(tagged.begin(), tagged.end(),
                         [&binding](const auto& item) { return item.first == binding.source; });
        if (source != tagged.end() && binding.field < table->entries.size()) {
            const app::FieldEntry& field = table->entries[binding.field];
            if (field.component != nullptr && field.component->read != nullptr) {
                if (const auto read = field.component->read(world, source->second, field.field)) {
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
            sprite.frame = focused ? button.focus_frame : button.normal_frame;
        });
}

} // namespace ge::mod::ui
