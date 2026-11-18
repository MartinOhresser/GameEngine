#include "ge/app/app.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/mod/inventory/inventory.gen.hpp"
#include "ge/mod/sprite2d/sprite2d.gen.hpp"
#include "ge/mod/text2d/text2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

#include <cstdint>
#include <vector>

namespace {

namespace inventory = ge::mod::inventory;
namespace sprite2d = ge::mod::sprite2d;
namespace text2d = ge::mod::text2d;
namespace transform2d = ge::mod::transform2d;

// The game's lists, as its manifest would give them.
constexpr std::uint32_t backpack = 0;
constexpr std::uint32_t chest = 1;
constexpr std::uint32_t sword = 0;
constexpr std::uint32_t potion = 1;
constexpr std::uint32_t key = 2;
constexpr std::uint32_t weapon = 1U << 0U;
constexpr std::uint32_t consumable = 1U << 1U;
constexpr std::uint32_t tool = 1U << 2U;

struct World {
    ge::app::App app;

    World() {
        transform2d::install(app);
        sprite2d::install(app);
        text2d::install(app);
        inventory::install(app);
        CHECK(app.module_problems().empty());
        CHECK(app.schedule().problems().empty());
        app.world().insert_resource(
            ge::app::ActionNames{{"left", "right", "up", "down", "select", "cancel", "pointer_x",
                                  "pointer_y", "click"}});
    }

    ge::ecs::Entity slot(std::uint32_t container, std::uint32_t index, std::uint32_t accepts = 0,
                         std::uint32_t max_stack = 0, bool locked = false) {
        const auto entity = app.world().spawn();
        CHECK(app.world().insert(entity,
                                 inventory::Slot{container, index, accepts, max_stack, locked}));
        CHECK(app.world().insert(entity,
                                 transform2d::Position{10.0F * static_cast<float>(index) +
                                                           100.0F * static_cast<float>(container),
                                                       5.0F}));
        return entity;
    }

    /// An item lying at the origin.
    ge::ecs::Entity item(std::uint32_t kind, std::uint32_t categories, std::uint32_t quantity = 1,
                         std::uint32_t max_stack = 99) {
        const auto entity = app.world().spawn();
        inventory::Item data;
        data.kind = kind;
        data.categories = categories;
        data.quantity = quantity;
        data.max_stack = max_stack;
        CHECK(app.world().insert(entity, data));
        CHECK(app.world().insert(entity, transform2d::Position{}));
        return entity;
    }

    void gatherer(std::uint32_t container) {
        const auto entity = app.world().spawn();
        CHECK(app.world().insert(entity, inventory::Gatherer{container, 16.0F}));
        CHECK(app.world().insert(entity, transform2d::Position{}));
    }

    [[nodiscard]] const inventory::Item* get(ge::ecs::Entity entity) const {
        return app.world().get<inventory::Item>(entity);
    }

    /// A request to move an item; returns the request.
    ge::ecs::Entity request(std::uint32_t from_container, std::uint32_t from_slot,
                            std::uint32_t to_container, std::uint32_t to_slot) {
        const auto entity = app.world().spawn();
        inventory::Transfer data;
        data.from_container = from_container;
        data.from_slot = from_slot;
        data.to_container = to_container;
        data.to_slot = to_slot;
        CHECK(app.world().insert(entity, data));
        return entity;
    }
};

bool in_slot(const inventory::Item* item, std::uint32_t container, std::uint32_t slot) {
    return item != nullptr && item->stored && item->container == container && item->slot == slot;
}

} // namespace

TEST_CASE("inventory: a gatherer takes what lies near it into the first slot that accepts it") {
    World w;
    w.slot(backpack, 0, weapon); // only weapons
    w.slot(backpack, 1, consumable);
    w.slot(backpack, 2);
    w.gatherer(backpack);
    const auto flask = w.item(potion, consumable);
    const auto blade = w.item(sword, weapon);
    const auto lockpick = w.item(key, tool);
    w.app.run_frames(1);
    CHECK(in_slot(w.get(flask), backpack, 1U)); // slot 0 refuses it: not a weapon
    CHECK(in_slot(w.get(blade), backpack, 0U));
    CHECK(in_slot(w.get(lockpick), backpack, 2U)); // the only one left that takes anything
}

TEST_CASE("inventory: items far from every gatherer stay where they are") {
    World w;
    w.slot(backpack, 0);
    w.gatherer(backpack);
    const auto far = w.item(potion, consumable);
    w.app.world().get<transform2d::Position>(far)->x = 500.0F;
    w.app.run_frames(2);
    CHECK(!w.get(far)->stored);
}

TEST_CASE("inventory: stacks of one kind merge up to what slot and item allow") {
    World w;
    w.slot(backpack, 0, 0, 2); // this slot never holds more than 2
    w.slot(backpack, 1);
    w.gatherer(backpack);
    const auto first = w.item(potion, consumable, 1, 10);
    w.app.run_frames(1);
    const auto second = w.item(potion, consumable, 1, 10);
    w.app.run_frames(1);
    CHECK(w.get(first)->quantity == 2U); // merged, the second entity is gone
    CHECK(!w.app.world().is_alive(second));
    const auto third = w.item(potion, consumable, 1, 10);
    w.app.run_frames(1);
    CHECK(w.get(first)->quantity == 2U);        // the slot is at its cap
    CHECK(in_slot(w.get(third), backpack, 1U)); // so the third takes the next slot
}

TEST_CASE("inventory: an unstackable kind takes one slot each, and a full bag refuses") {
    World w;
    w.slot(backpack, 0);
    w.slot(backpack, 1);
    w.gatherer(backpack);
    const auto a = w.item(sword, weapon, 1, 1);
    const auto b = w.item(sword, weapon, 1, 1);
    const auto c = w.item(sword, weapon, 1, 1);
    w.app.run_frames(2);
    CHECK(in_slot(w.get(a), backpack, 0U));
    CHECK(in_slot(w.get(b), backpack, 1U));
    CHECK(!w.get(c)->stored); // nowhere left: it stays in the world
    CHECK(w.app.world().is_alive(c));
}

TEST_CASE("inventory: a locked slot receives nothing, and each container is its own") {
    World w;
    w.slot(backpack, 0, 0, 0, true);
    w.slot(backpack, 1);
    w.slot(chest, 0);
    w.gatherer(chest);
    const auto in_chest = w.item(potion, consumable);
    w.app.run_frames(1);
    CHECK(in_slot(w.get(in_chest), chest, 0U));
    World locked;
    locked.slot(backpack, 0, 0, 0, true);
    locked.slot(backpack, 1);
    locked.gatherer(backpack);
    const auto skipped = locked.item(potion, consumable);
    locked.app.run_frames(1);
    CHECK(in_slot(locked.get(skipped), backpack, 1U));
}

TEST_CASE("inventory: a transfer moves an item, answers, then disappears") {
    World w;
    w.slot(backpack, 0);
    w.slot(backpack, 1);
    w.slot(chest, 0, weapon); // a chest slot for weapons only
    w.gatherer(backpack);
    const auto blade = w.item(sword, weapon);
    w.app.run_frames(1);
    REQUIRE(in_slot(w.get(blade), backpack, 0U));
    const auto request = w.request(backpack, 0, chest, 0);
    w.app.run_frames(1);
    CHECK(in_slot(w.get(blade), chest, 0U));
    CHECK(w.app.world().get<inventory::Transfer>(request)->status == 1U); // done
    w.app.run_frames(1);
    CHECK(!w.app.world().is_alive(request)); // answered a tick ago: gone
}

TEST_CASE("inventory: a transfer is refused when the slot does not suit the item") {
    World w;
    w.slot(backpack, 0);
    w.slot(chest, 0, weapon);
    w.gatherer(backpack);
    const auto flask = w.item(potion, consumable);
    w.app.run_frames(1);
    const auto request = w.request(backpack, 0, chest, 0);
    w.app.run_frames(1);
    CHECK(in_slot(w.get(flask), backpack, 0U)); // a potion is no weapon
    CHECK(w.app.world().get<inventory::Transfer>(request)->status == 2U);
    const auto nothing = w.request(backpack, 3, chest, 0); // no such slot
    w.app.run_frames(1);
    CHECK(w.app.world().get<inventory::Transfer>(nothing)->status == 2U);
}

TEST_CASE("inventory: different kinds swap only when both slots take the other item") {
    World w;
    w.slot(backpack, 0);         // anything
    w.slot(backpack, 1, weapon); // weapons only
    w.gatherer(backpack);
    const auto flask = w.item(potion, consumable);
    const auto blade = w.item(sword, weapon);
    w.app.run_frames(1);
    REQUIRE(in_slot(w.get(flask), backpack, 0U));
    REQUIRE(in_slot(w.get(blade), backpack, 1U));
    w.request(backpack, 1, backpack,
              0); // the sword onto the potion: the potion cannot go to slot 1
    w.app.run_frames(1);
    CHECK(in_slot(w.get(flask), backpack, 0U));
    CHECK(in_slot(w.get(blade), backpack, 1U));

    World open;
    open.slot(backpack, 0);
    open.slot(backpack, 1);
    open.gatherer(backpack);
    const auto a = open.item(potion, consumable);
    const auto b = open.item(sword, weapon);
    open.app.run_frames(1);
    open.request(backpack, 0, backpack, 1);
    open.app.run_frames(1);
    CHECK(in_slot(open.get(a), backpack, 1U)); // swapped
    CHECK(in_slot(open.get(b), backpack, 0U));
}

TEST_CASE("inventory: a transfer onto the same kind merges as far as there is room") {
    World w;
    w.slot(backpack, 0, 0, 5);
    w.slot(backpack, 1, 0, 5);
    const auto left = w.item(potion, consumable, 4, 99);
    const auto right = w.item(potion, consumable, 3, 99);
    for (const auto& [entity, slot] : {std::pair{left, 0U}, std::pair{right, 1U}}) {
        auto* data = w.app.world().get<inventory::Item>(entity);
        data->stored = true;
        data->container = backpack;
        data->slot = slot;
    }
    w.request(backpack, 1, backpack, 0);
    w.app.run_frames(1);
    CHECK(w.get(left)->quantity == 5U);  // the slot holds 5 at most
    CHECK(w.get(right)->quantity == 2U); // what did not fit stays behind
    CHECK(in_slot(w.get(right), backpack, 1U));
}

TEST_CASE("inventory: a stored item is shown on its slot, as part of the interface") {
    World w;
    w.slot(backpack, 0);
    const auto slot_two = w.slot(backpack, 1);
    w.gatherer(backpack);
    const auto flask = w.item(potion, consumable, 3);
    CHECK(w.app.world().insert(flask, sprite2d::Sprite{}));
    CHECK(w.app.world().insert(flask, text2d::Text{}));
    w.app.run_frames(1);
    const auto* sprite = w.app.world().get<sprite2d::Sprite>(flask);
    CHECK(sprite->screen); // stored: drawn over the window, not in the world
    CHECK(w.app.world().get<text2d::Text>(flask)->screen);
    CHECK(w.app.world().get<text2d::Text>(flask)->value == 3.0F);      // the quantity
    CHECK(w.app.world().get<transform2d::Position>(flask)->x == 0.0F); // slot 0
    w.request(backpack, 0, backpack, 1);
    w.app.run_frames(1);
    CHECK(w.app.world().get<transform2d::Position>(flask)->x ==
          w.app.world().get<transform2d::Position>(slot_two)->x);
}

/// A hotbar of four slots, two to a row, with a potion in slot 0 and a Hand over it.
/// Slot 2 takes weapons only.
struct HandWorld {
    World w;
    ge::ecs::Entity flask;
    ge::ecs::Entity cursor;
    std::uint64_t at = 0;

    HandWorld() {
        w.slot(backpack, 0);
        w.slot(backpack, 1);
        w.slot(backpack, 2, weapon);
        w.slot(backpack, 3);
        w.gatherer(backpack);
        flask = w.item(potion, consumable);
        w.app.run_frames(1);
        cursor = w.app.world().spawn();
        inventory::Hand hand;
        hand.container = backpack;
        hand.columns = 2; // two slots to a row
        hand.left = 0;
        hand.right = 1;
        hand.up = 2;
        hand.down = 3;
        hand.select = 4;
        hand.cancel = 5;
        CHECK(w.app.world().insert(cursor, hand));
        CHECK(w.app.world().insert(cursor, transform2d::Position{}));
        at = w.app.tick_count();
    }

    /// Presses an action for one tick, then lets a tick of rest follow.
    void press(const char* action) {
        auto* script = w.app.world().resource<ge::app::ScriptedInput>();
        script->add(at, action, 1.0F);
        script->add(at + 1, action, 0.0F);
        at += 2;
    }

    [[nodiscard]] const inventory::Hand& hand() const {
        return *w.app.world().get<inventory::Hand>(cursor);
    }
};

TEST_CASE("inventory: a hand picks an item up and puts it down") {
    HandWorld h;
    REQUIRE(in_slot(h.w.get(h.flask), backpack, 0U));
    h.press("select"); // pick up the potion at slot 0
    h.press("right");  // to slot 1
    h.press("down");   // to slot 3 (two to a row)
    h.w.app.run_frames(6);
    CHECK(h.hand().holding);
    h.press("select"); // put it there
    h.w.app.run_frames(2);
    CHECK(!h.hand().holding);
    CHECK(in_slot(h.w.get(h.flask), backpack, 3U));
    CHECK(h.w.app.world().get<transform2d::Position>(h.cursor)->x == 30.0F); // sits on slot 3
}

TEST_CASE("inventory: a hand obeys the slots' rules, and can let go") {
    HandWorld h;
    h.press("select");
    h.press("down"); // slot 2, which takes weapons only
    h.press("select");
    h.w.app.run_frames(6);
    CHECK(!h.hand().holding);
    CHECK(in_slot(h.w.get(h.flask), backpack, 0U)); // refused: still where it was
    h.press("select");
    h.press("cancel"); // let go without putting down
    h.w.app.run_frames(4);
    CHECK(!h.hand().holding);
    CHECK(in_slot(h.w.get(h.flask), backpack, 0U));
}
