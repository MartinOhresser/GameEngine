#include "ge/app/app.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/mod/loadout/loadout.gen.hpp"
#include "ge/mod/reserve/reserve.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

#include <cmath>
#include <cstdint>
#include <string>

namespace {

namespace loadout = ge::mod::loadout;
namespace reserve = ge::mod::reserve;
namespace transform2d = ge::mod::transform2d;

constexpr std::uint32_t none = 0xFFFFFFFFU;
// The powers of these tests, as in a game's power list.
constexpr std::uint32_t fire = 0U;
constexpr std::uint32_t ice = 1U;
constexpr std::uint32_t wind = 2U;
constexpr std::uint32_t steam = 3U;
// The actions, as in a game's action list.
constexpr std::uint32_t left_button = 0U;
constexpr std::uint32_t right_button = 1U;
constexpr std::uint32_t mana = 0U;

struct Table {
    ge::app::App app;
    std::uint64_t tick = 0;
    ge::ecs::Entity left;
    ge::ecs::Entity right;
    ge::ecs::Entity kit;

    explicit Table(std::uint32_t unlocked = 0b0111U, std::uint32_t offered = 0U) {
        transform2d::install(app);
        reserve::install(app);
        loadout::install(app);
        CHECK(app.module_problems().empty());
        CHECK(app.schedule().problems().empty());
        app.world().insert_resource(ge::app::ActionNames{{"left", "right", "change"}});
        loadout::Kit rules;
        rules.unlocked = unlocked;
        rules.offered = offered;
        kit = app.world().spawn();
        CHECK(app.world().insert(kit, rules));
        left = hand(0U, left_button);
        right = hand(1U, right_button);
    }

    ge::ecs::Entity hand(std::uint32_t slot, std::uint32_t trigger) {
        const ge::ecs::Entity entity = app.world().spawn();
        loadout::Hand made;
        made.slot = slot;
        made.trigger = trigger;
        CHECK(app.world().insert(entity, made));
        return entity;
    }

    ge::ecs::Entity ability(std::uint32_t power, float per_use = 0.0F, float per_second = 0.0F) {
        const ge::ecs::Entity entity = app.world().spawn();
        CHECK(app.world().insert(entity, loadout::Ability{power, mana, per_use, per_second}));
        CHECK(app.world().insert(entity, transform2d::Disabled{}));
        return entity;
    }

    void set(const char* action, float value, std::uint64_t later = 0) {
        app.world().resource<ge::app::ScriptedInput>()->add(tick + later, action, value);
    }
    void run(std::uint64_t ticks) {
        app.run_frames(static_cast<std::uint32_t>(ticks));
        tick += ticks;
    }
    /// Presses and lets go of an action.
    void tap(const char* action) {
        set(action, 1.0F);
        set(action, 0.0F, 1);
        run(2);
    }
    loadout::Hand& hand_of(ge::ecs::Entity entity) { return *app.world().get<loadout::Hand>(entity); }
    loadout::Kit& rules() { return *app.world().get<loadout::Kit>(kit); }
    bool working(ge::ecs::Entity effect) { return !app.world().has<transform2d::Disabled>(effect); }
};

} // namespace

TEST_CASE("loadout: change and a trigger give the next power, swapping with the other hand") {
    Table table;
    CHECK(table.hand_of(table.left).item == none);
    table.set("change", 1.0F);
    table.run(1);
    table.tap("left");
    CHECK(table.hand_of(table.left).item == fire); // empty, then the first power
    table.tap("left");
    CHECK(table.hand_of(table.left).item == ice);
    table.tap("right");
    CHECK(table.hand_of(table.right).item == fire);
    table.tap("right"); // fire, then ice: the left hand held it, it takes fire
    CHECK(table.hand_of(table.right).item == ice);
    CHECK(table.hand_of(table.left).item == fire);
    table.tap("right");
    table.tap("right"); // wind, then round to fire again: swapped with the left hand
    CHECK(table.hand_of(table.right).item == fire);
    CHECK(table.hand_of(table.left).item == wind);
    CHECK(!table.rules().wheel_open); // a trigger was used: no wheel during this hold
    table.set("change", 0.0F);
    table.run(30);
    CHECK(!table.rules().wheel_open);
}

TEST_CASE("loadout: holding change opens the wheel, which slows the game and chooses") {
    Table table(0b0111U, 0b1000U); // fire, ice and wind work; steam may only be held
    table.set("pointer_x", 400.0F);
    table.set("pointer_y", 300.0F);
    table.set("change", 1.0F);
    table.run(10);
    CHECK(!table.rules().wheel_open);
    table.run(15); // past a third of a second
    CHECK(table.rules().wheel_open);
    CHECK(std::abs(table.app.world().resource<transform2d::Params>()->speed_scale - 0.2F) < 1e-6F);
    CHECK(table.rules().wheel_choice == none); // the pointer has not moved
    // Four powers around the circle: fire at the top, ice on the right, wind below, steam left.
    table.set("pointer_x", 300.0F);
    table.run(1);
    CHECK(table.rules().wheel_choice == steam);
    table.set("pointer_x", 400.0F);
    table.set("pointer_y", 380.0F);
    table.run(1);
    CHECK(table.rules().wheel_choice == wind);
    table.tap("left");
    CHECK(table.hand_of(table.left).item == wind);
    table.set("change", 0.0F);
    table.run(1);
    CHECK(!table.rules().wheel_open);
    CHECK(table.app.world().resource<transform2d::Params>()->speed_scale == 1.0F);
}

TEST_CASE("loadout: an ability works while its hand is held, and pays for it") {
    Table table;
    const ge::ecs::Entity stock = table.app.world().spawn();
    CHECK(table.app.world().insert(stock, reserve::Reserve{mana, 20.0F, 100.0F}));
    table.hand_of(table.left).item = fire;
    table.hand_of(table.right).item = wind;
    const ge::ecs::Entity flame = table.ability(fire, 5.0F, 6.0F);
    const ge::ecs::Entity gust = table.ability(wind);
    table.set("left", 1.0F);
    table.run(1);
    CHECK(table.working(flame));
    CHECK(!table.working(gust));
    table.run(60); // 5 to start, then 6 a second for a second
    CHECK(std::abs(table.app.world().get<reserve::Reserve>(stock)->amount - 9.0F) < 0.05F);
    CHECK(table.app.world().get<loadout::Ability>(flame)->time > 0.95F);
    table.run(120); // runs out of mana and stops
    CHECK(!table.working(flame));
    table.set("left", 0.0F);
    table.run(1);
    table.app.world().get<reserve::Reserve>(stock)->amount = 2.0F;
    table.tap("left"); // 5 to start: refused
    CHECK(!table.working(flame));
    CHECK(table.app.world().get<loadout::Ability>(flame)->refused == 1U);
    CHECK(table.app.world().get<loadout::Ability>(flame)->uses == 1U);
}

TEST_CASE("loadout: a power held but not unlocked does nothing") {
    Table table(0b0001U, 0b0010U);
    table.hand_of(table.left).item = ice;
    const ge::ecs::Entity frost = table.ability(ice);
    table.set("left", 1.0F);
    table.run(10);
    CHECK(table.hand_of(table.left).acting);
    CHECK(!table.working(frost));
}

TEST_CASE("loadout: both triggers together make the combined power, not the two alone") {
    Table table;
    const ge::ecs::Entity mix = table.app.world().spawn();
    CHECK(table.app.world().insert(mix, loadout::Combo{fire, ice, wind}));
    table.hand_of(table.left).item = ice;
    table.hand_of(table.right).item = fire;
    const ge::ecs::Entity flame = table.ability(fire);
    const ge::ecs::Entity frost = table.ability(ice);
    const ge::ecs::Entity gust = table.ability(wind);
    table.set("left", 1.0F);
    table.set("right", 1.0F, 3); // three ticks later: within a tenth of a second
    table.run(2);
    CHECK(!table.working(frost)); // the left hand waits for a possible second hand
    table.run(3);
    CHECK(table.working(gust));
    CHECK(!table.working(flame));
    CHECK(!table.working(frost));
    CHECK(table.hand_of(table.left).joined);
    table.set("right", 0.0F);
    table.run(2);
    CHECK(!table.working(gust));
    CHECK(!table.working(frost)); // the left hand waits to be let go
    CHECK(table.hand_of(table.left).waiting);
    table.set("left", 0.0F);
    table.run(1);
    table.set("left", 1.0F);
    table.run(10); // alone: it acts once the tenth of a second has passed
    CHECK(table.working(frost));
}

TEST_CASE("loadout: a held shortcut uses two powers together, then gives the hands back") {
    Table table;
    const ge::ecs::Entity mix = table.app.world().spawn();
    CHECK(table.app.world().insert(mix, loadout::Combo{fire, ice, steam}));
    table.app.world().insert_resource(ge::app::ActionNames{{"left", "right", "change", "gather"}});
    const ge::ecs::Entity key = table.app.world().spawn();
    CHECK(table.app.world().insert(key, loadout::Shortcut{3U, fire, ice}));
    table.hand_of(table.left).item = wind;
    const ge::ecs::Entity cloud = table.ability(steam);
    const ge::ecs::Entity gust = table.ability(wind);
    table.set("gather", 1.0F);
    table.run(2);
    CHECK(table.working(cloud));
    CHECK(!table.working(gust));
    CHECK(table.hand_of(table.left).item == fire);
    CHECK(table.hand_of(table.right).item == ice);
    table.set("gather", 0.0F);
    table.run(2);
    CHECK(!table.working(cloud));
    CHECK(table.hand_of(table.left).item == wind);
    CHECK(table.hand_of(table.right).item == none);
}
