#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/mod/inventory/inventory.gen.hpp"
#include "ge/mod/sprite2d/sprite2d.gen.hpp"
#include "ge/mod/text2d/text2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace ge::mod::inventory {

namespace {

constexpr std::uint32_t pending = 0;
constexpr std::uint32_t done = 1;
constexpr std::uint32_t refused = 2;

/// Where an item can be: the slot with this address, if the game made one.
std::optional<ecs::Entity> find_slot(ecs::World& world, std::uint32_t container,
                                     std::uint32_t index) {
    std::optional<ecs::Entity> found;
    world.each<const Slot>([&](ecs::Entity entity, const Slot& slot) {
        if (!found && slot.container == container && slot.index == index) {
            found = entity;
        }
    });
    return found;
}

/// The stored item that sits in the slot at this address, if any.
std::optional<ecs::Entity> item_in(ecs::World& world, std::uint32_t container,
                                   std::uint32_t index) {
    std::optional<ecs::Entity> found;
    world.each<const Item>([&](ecs::Entity entity, const Item& item) {
        if (!found && item.stored && item.container == container && item.slot == index) {
            found = entity;
        }
    });
    return found;
}

bool takes(const Slot& slot, const Item& item) {
    return !slot.locked && (slot.accepts == 0U || (slot.accepts & item.categories) != 0U);
}

/// The most this slot holds of this item: its own cap and the item's.
std::uint32_t capacity(const Slot& slot, const Item& item) {
    return slot.max_stack == 0U ? item.max_stack : std::min(slot.max_stack, item.max_stack);
}

/// The slots of a container, in index order.
std::vector<std::pair<std::uint32_t, ecs::Entity>> slots_of(ecs::World& world,
                                                            std::uint32_t container) {
    std::vector<std::pair<std::uint32_t, ecs::Entity>> slots;
    world.each<const Slot>([&](ecs::Entity entity, const Slot& slot) {
        if (slot.container == container) {
            slots.emplace_back(slot.index, entity);
        }
    });
    std::sort(slots.begin(), slots.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    return slots;
}

/// Puts a loose item into the container. Returns true when nothing is left of
/// it (it joined stacks, or took a slot); otherwise what did not fit stays in
/// `quantity` and the item stays loose.
bool add_to(ecs::World& world, std::uint32_t container, ecs::Entity entity, bool& placed) {
    Item* item = world.get<Item>(entity);
    placed = false;
    if (item == nullptr) {
        return false;
    }
    const auto slots = slots_of(world, container);
    // First the stacks of the same kind that have room.
    for (const auto& [index, slot_entity] : slots) {
        const Slot* slot = world.get<Slot>(slot_entity);
        const auto other = item_in(world, container, index);
        if (slot == nullptr || !other || !takes(*slot, *item)) {
            continue;
        }
        Item* stack = world.get<Item>(*other);
        if (stack == nullptr || stack->kind != item->kind) {
            continue;
        }
        const std::uint32_t room =
            std::max(capacity(*slot, *item), stack->quantity) - stack->quantity;
        const std::uint32_t moved = std::min(room, item->quantity);
        stack->quantity += moved;
        item->quantity -= moved;
        if (item->quantity == 0U) {
            return true; // merged into an existing stack: this entity goes away
        }
    }
    // Then the first empty slot that takes it.
    for (const auto& [index, slot_entity] : slots) {
        const Slot* slot = world.get<Slot>(slot_entity);
        if (slot == nullptr || item_in(world, container, index) || !takes(*slot, *item) ||
            item->quantity > capacity(*slot, *item)) {
            continue;
        }
        item->stored = true;
        item->container = container;
        item->slot = index;
        placed = true;
        return false; // it lives on, in its slot
    }
    return false;
}

/// Carries out one move; returns `done` or `refused`.
std::uint32_t move_item(ecs::World& world, const Transfer& request) {
    const auto from = find_slot(world, request.from_container, request.from_slot);
    const auto to = find_slot(world, request.to_container, request.to_slot);
    const auto moving = item_in(world, request.from_container, request.from_slot);
    if (!from || !to || !moving || *from == *to) {
        return refused;
    }
    const Slot* source = world.get<Slot>(*from);
    const Slot* target = world.get<Slot>(*to);
    Item* item = world.get<Item>(*moving);
    if (source == nullptr || target == nullptr || item == nullptr || source->locked ||
        !takes(*target, *item)) {
        return refused;
    }
    const auto other = item_in(world, request.to_container, request.to_slot);
    if (!other) {
        if (item->quantity > capacity(*target, *item)) {
            return refused; // a stack is moved whole: it must fit
        }
        item->container = request.to_container;
        item->slot = request.to_slot;
        return done;
    }
    Item* there = world.get<Item>(*other);
    if (there == nullptr) {
        return refused;
    }
    if (there->kind == item->kind) {
        const std::uint32_t room =
            std::max(capacity(*target, *item), there->quantity) - there->quantity;
        if (room == 0U) {
            return refused;
        }
        const std::uint32_t moved = std::min(room, item->quantity);
        there->quantity += moved;
        item->quantity -= moved;
        if (item->quantity == 0U) {
            static_cast<void>(world.despawn(*moving));
        }
        return done;
    }
    // Another kind: the two swap, if each slot takes the other's item.
    if (!takes(*source, *there) || there->quantity > capacity(*source, *there) ||
        item->quantity > capacity(*target, *item)) {
        return refused;
    }
    there->container = request.from_container;
    there->slot = request.from_slot;
    item->container = request.to_container;
    item->slot = request.to_slot;
    return done;
}

std::uint32_t pressed_count(const app::Actions& actions, const app::ActionNames* names,
                            std::uint32_t action) {
    return names != nullptr && action < names->names.size() && actions.pressed(names->names[action])
               ? 1U
               : 0U;
}

float value_of(const app::Actions& actions, const app::ActionNames* names, std::uint32_t action) {
    return names != nullptr && action < names->names.size() ? actions.value(names->names[action])
                                                            : 0.0F;
}

/// The slot (by index) of a container that the pointer is over.
std::optional<std::uint32_t> slot_under(ecs::World& world, const Hand& hand, float x, float y) {
    std::optional<std::uint32_t> found;
    world.each<const Slot, const transform2d::Position>(
        [&](ecs::Entity, const Slot& slot, const transform2d::Position& position) {
            if (!found && slot.container == hand.container &&
                std::fabs(x - position.x) <= hand.slot_width / 2.0F &&
                std::fabs(y - position.y) <= hand.slot_height / 2.0F) {
                found = slot.index;
            }
        });
    return found;
}

/// Moves the cursor with the direction actions: columns slots to a row.
std::uint32_t moved_cursor(const Hand& hand, const app::Actions& actions,
                           const app::ActionNames* names, std::uint32_t count) {
    const std::uint32_t columns = std::max<std::uint32_t>(hand.columns, 1U);
    std::uint32_t cursor = std::min(hand.cursor, count - 1U);
    if (pressed_count(actions, names, hand.left) != 0U && cursor % columns != 0U) {
        --cursor;
    }
    if (pressed_count(actions, names, hand.right) != 0U && cursor % columns != columns - 1U &&
        cursor + 1U < count) {
        ++cursor;
    }
    if (pressed_count(actions, names, hand.up) != 0U && cursor >= columns) {
        cursor -= columns;
    }
    if (pressed_count(actions, names, hand.down) != 0U && cursor + columns < count) {
        cursor += columns;
    }
    return cursor;
}

void run_hand(ecs::World& world, Hand& hand, const app::Actions& actions,
              const app::ActionNames* names) {
    const auto slots = slots_of(world, hand.container);
    if (slots.empty()) {
        return;
    }
    const auto count = static_cast<std::uint32_t>(slots.size());
    hand.cursor = moved_cursor(hand, actions, names, count);
    bool clicked = false;
    if (hand.use_pointer) {
        const float x = value_of(actions, names, hand.pointer_x);
        const float y = value_of(actions, names, hand.pointer_y);
        const auto over = slot_under(world, hand, x, y);
        if (over && (x != hand.last_x || y != hand.last_y ||
                     pressed_count(actions, names, hand.click) != 0U)) {
            hand.cursor = *over;
        }
        clicked = over && pressed_count(actions, names, hand.click) != 0U;
        hand.last_x = x;
        hand.last_y = y;
    }
    if (pressed_count(actions, names, hand.cancel) != 0U) {
        hand.holding = false;
    }
    if (clicked || pressed_count(actions, names, hand.select) != 0U) {
        if (!hand.holding) {
            const auto item = item_in(world, hand.container, hand.cursor);
            const auto slot = find_slot(world, hand.container, hand.cursor);
            const Slot* at = slot ? world.get<Slot>(*slot) : nullptr;
            hand.holding = item.has_value() && at != nullptr && !at->locked;
            hand.held_slot = hand.cursor;
        } else {
            Transfer request;
            request.from_container = hand.container;
            request.from_slot = hand.held_slot;
            request.to_container = hand.container;
            request.to_slot = hand.cursor;
            static_cast<void>(move_item(world, request));
            hand.holding = false;
        }
    }
}

} // namespace

void hand(ecs::World& world) {
    const auto* actions = world.resource<app::Actions>();
    const auto* names = world.resource<app::ActionNames>();
    if (actions == nullptr) {
        return;
    }
    world.each<Hand>([&](ecs::Entity entity, Hand& cursor) {
        run_hand(world, cursor, *actions, names);
        // The frame sits on the slot under the cursor and shows whether an item is held.
        if (const auto slot = find_slot(world, cursor.container, cursor.cursor); slot) {
            const auto* target = world.get<transform2d::Position>(*slot);
            auto* mine = world.get<transform2d::Position>(entity);
            if (target != nullptr && mine != nullptr) {
                *mine = *target;
            }
        }
        if (auto* sprite = world.get<sprite2d::Sprite>(entity); sprite != nullptr) {
            sprite->frame = cursor.holding ? cursor.holding_frame : cursor.cursor_frame;
        }
    });
}

void transfer(ecs::World& world) {
    std::vector<ecs::Entity> answered;
    std::vector<ecs::Entity> waiting;
    world.each<const Transfer>([&](ecs::Entity entity, const Transfer& request) {
        (request.status == pending ? waiting : answered).push_back(entity);
    });
    for (const ecs::Entity entity : answered) {
        static_cast<void>(world.despawn(entity)); // it was visible for a whole tick
    }
    for (const ecs::Entity entity : waiting) {
        const Transfer* request = world.get<Transfer>(entity);
        if (request == nullptr) {
            continue;
        }
        const std::uint32_t status = move_item(world, *request);
        if (auto* mine = world.get<Transfer>(entity); mine != nullptr) {
            mine->status = status;
        }
    }
}

void gather(ecs::World& world) {
    struct Reach {
        std::uint32_t container;
        float radius;
        transform2d::Position at;
    };
    std::vector<Reach> gatherers;
    world.each<const Gatherer, const transform2d::Position>(
        [&gatherers](ecs::Entity, const Gatherer& gatherer, const transform2d::Position& position) {
            gatherers.push_back(Reach{gatherer.container, gatherer.radius, position});
        });
    if (gatherers.empty()) {
        return;
    }
    std::vector<std::pair<ecs::Entity, std::uint32_t>> candidates; // loose item, container
    world.each<const Item, const transform2d::Position>(
        [&](ecs::Entity entity, const Item& item, const transform2d::Position& position) {
            if (item.stored) {
                return;
            }
            for (const Reach& reach : gatherers) {
                const float dx = position.x - reach.at.x;
                const float dy = position.y - reach.at.y;
                if (dx * dx + dy * dy <= reach.radius * reach.radius) {
                    candidates.emplace_back(entity, reach.container);
                    return;
                }
            }
        });
    for (const auto& [entity, container] : candidates) {
        bool placed = false;
        if (add_to(world, container, entity, placed)) {
            static_cast<void>(world.despawn(entity));
        }
    }
}

void show(ecs::World& world) {
    world.each<const Item>([&](ecs::Entity entity, const Item& item) {
        if (auto* sprite = world.get<sprite2d::Sprite>(entity); sprite != nullptr) {
            sprite->screen = item.stored;
        }
        if (auto* text = world.get<text2d::Text>(entity); text != nullptr) {
            text->screen = item.stored;
            text->value = static_cast<float>(item.quantity);
        }
        if (!item.stored) {
            return;
        }
        const auto slot = find_slot(world, item.container, item.slot);
        const auto* target = slot ? world.get<transform2d::Position>(*slot) : nullptr;
        auto* mine = world.get<transform2d::Position>(entity);
        if (target != nullptr && mine != nullptr) {
            *mine = *target;
        }
    });
}

} // namespace ge::mod::inventory
