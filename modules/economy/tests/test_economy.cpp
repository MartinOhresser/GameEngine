#include "ge/app/app.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/app/spawn.hpp"
#include "ge/mod/camera2d/camera2d.gen.hpp"
#include "ge/mod/economy/economy.gen.hpp"
#include "ge/mod/grid2d/grid2d.gen.hpp"
#include "ge/mod/health/health.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace economy = ge::mod::economy;
namespace grid2d = ge::mod::grid2d;
namespace health = ge::mod::health;
namespace transform2d = ge::mod::transform2d;

using ge::app::SpawnRequest;
using ge::ecs::Entity;

// Towers of the tests: prefab 1 is a tower of level 1 that can be improved to prefab 2 for 30
// and sold for 20; prefab 2 is a final tower, sold for 45. The board is 5 by 5, cells of 10.
constexpr std::uint32_t basic = 1;
constexpr std::uint32_t better = 2;

// Actions: 0 placed, 1 refused, 2 selected, 3 final, 4 deselected, 5 upgrade, 6 sell,
// 7 pointer_x, 8 pointer_y, 9 click.
struct Scene {
    ge::app::App app;
    std::shared_ptr<std::vector<SpawnRequest>> asked =
        std::make_shared<std::vector<SpawnRequest>>();
    std::map<std::string, int> seen; ///< How many ticks each action was pressed in.
    Entity purse;
    Entity cursor;
    std::uint64_t tick = 1;

    Scene() {
        transform2d::install(app);
        ge::mod::camera2d::install(app);
        grid2d::install(app);
        health::install(app);
        economy::install(app);
        CHECK(app.module_problems().empty());
        CHECK(app.schedule().problems().empty());
        app.world().insert_resource(
            ge::app::ActionNames{{"placed", "refused", "selected", "final", "deselected", "upgrade",
                                  "sell", "pointer_x", "pointer_y", "click"}});
        app.set_prefab_spawner([asked = asked](ge::app::App& game, const SpawnRequest& request) {
            asked->push_back(request);
            make_tower(game, request);
        });
        const Entity board = app.world().spawn();
        CHECK(app.world().insert(board, transform2d::Position{0.0F, 0.0F}));
        grid2d::Grid grid;
        grid.columns = 5;
        grid.rows = 5;
        grid.cell_width = 10.0F;
        grid.cell_height = 10.0F;
        CHECK(app.world().insert(board, grid));
        purse = app.world().spawn();
        CHECK(app.world().insert(purse, economy::Wallet{100}));
        cursor = app.world().spawn();
        CHECK(app.world().insert(cursor, transform2d::Position{0.0F, 0.0F}));
        grid2d::GridCursor look;
        look.pointer_x = 7;
        look.pointer_y = 8;
        look.click = 9;
        CHECK(app.world().insert(cursor, look));
        economy::Placer placer;
        placer.prefab = basic;
        placer.price = 40;
        placer.placed_action = 0;
        placer.refused_action = 1;
        placer.selected_action = 2;
        placer.final_action = 3;
        placer.deselected_action = 4;
        placer.upgrade_action = 5;
        placer.sell_action = 6;
        CHECK(app.world().insert(cursor, placer));
        play(2);
    }

    /// What the game does with a request to create a tower: the entity of the cell it names.
    static void make_tower(ge::app::App& game, const SpawnRequest& request) {
        const Entity entity = game.world().spawn();
        CHECK(game.world().insert(entity, transform2d::Position{request.x, request.y}));
        grid2d::GridPos at;
        at.col = static_cast<std::int32_t>(std::lround(request.x / 10.0F)) + 2;
        at.row = static_cast<std::int32_t>(std::lround(request.y / 10.0F)) + 2;
        CHECK(game.world().insert(entity, at));
        if (request.prefab == basic) {
            CHECK(game.world().insert(entity, economy::Upgradable{better, 30}));
            CHECK(game.world().insert(entity, economy::Sellable{20}));
        } else {
            CHECK(game.world().insert(entity, economy::Sellable{45}));
        }
    }

    void play(int frames) {
        for (int i = 0; i < frames; ++i) {
            app.run_frames(1);
            ++tick;
            for (const char* name : {"placed", "refused", "selected", "final", "deselected"}) {
                seen[name] += app.world().resource<ge::app::Actions>()->pressed(name) ? 1 : 0;
            }
        }
    }

    /// A touch on a cell, and a few ticks for the game to answer.
    void touch(std::int32_t col, std::int32_t row) {
        auto* input = app.world().resource<ge::app::ScriptedInput>();
        input->add(tick, "pointer_x", static_cast<float>(col - 2) * 10.0F);
        input->add(tick, "pointer_y", static_cast<float>(row - 2) * 10.0F);
        input->add(tick + 1, "click", 1.0F);
        input->add(tick + 2, "click", 0.0F);
        play(6);
    }

    void press(const char* action) {
        app.world().resource<ge::app::Actions>()->raise(action);
        play(4);
    }

    [[nodiscard]] int wallet() const { return app.world().get<economy::Wallet>(purse)->amount; }
    [[nodiscard]] economy::Placer& placer() { return *app.world().get<economy::Placer>(cursor); }
    [[nodiscard]] bool cursor_shown() const {
        return !app.world().has<transform2d::Hidden>(cursor);
    }

    /// How many towers stand on the board.
    [[nodiscard]] std::size_t towers() {
        std::size_t count = 0;
        app.world().each<const economy::Sellable>(
            [&count](Entity, const economy::Sellable&) { ++count; });
        return count;
    }
};

} // namespace

TEST_CASE("economy: a click on a free cell builds the chosen tower and takes its price") {
    Scene scene;
    scene.touch(1, 3);
    REQUIRE(scene.asked->size() == 1U);
    CHECK(scene.asked->front().prefab == basic);
    CHECK(scene.asked->front().x == -10.0F);
    CHECK(scene.asked->front().y == 10.0F);
    CHECK(scene.wallet() == 60);
    CHECK(scene.seen["placed"] == 1);
    CHECK(scene.towers() == 1U);
}

TEST_CASE("economy: not enough money builds nothing") {
    Scene scene;
    scene.app.world().get<economy::Wallet>(scene.purse)->amount = 30;
    scene.touch(1, 3);
    CHECK(scene.asked->empty());
    CHECK(scene.wallet() == 30);
    CHECK(scene.seen["refused"] == 1);
    CHECK(scene.seen["placed"] == 0);
}

TEST_CASE("economy: nothing is built on a blocked cell, on a tower, or without a chosen prefab") {
    Scene scene;
    const Entity rock = scene.app.world().spawn();
    grid2d::GridPos at;
    at.col = 4;
    at.row = 4;
    CHECK(scene.app.world().insert(rock, at));
    CHECK(scene.app.world().insert(rock, grid2d::Blocked{}));
    scene.touch(4, 4);
    CHECK(scene.asked->empty());
    scene.touch(0, 0);
    CHECK(scene.asked->size() == 1U);
    scene.touch(0, 0); // the tower is there now: it is selected, not replaced
    CHECK(scene.asked->size() == 1U);
    CHECK(scene.wallet() == 60);
    scene.placer().prefab = 0xFFFFFFFFU;
    scene.touch(2, 2);
    CHECK(scene.asked->size() == 1U);
    CHECK(scene.wallet() == 60);
}

TEST_CASE("economy: no wallet, nothing is built") {
    Scene scene;
    CHECK(scene.app.world().despawn(scene.purse));
    scene.touch(1, 1);
    CHECK(scene.asked->empty());
}

TEST_CASE("economy: a click on a tower selects it and shows what can be done with it") {
    Scene scene;
    scene.touch(1, 3);
    CHECK(!scene.cursor_shown()); // a tower was built: nothing is selected yet
    scene.touch(1, 3);
    CHECK(scene.seen["selected"] == 1);
    CHECK(scene.placer().selected != 0U);
    CHECK(scene.placer().upgrade_price == 30);
    CHECK(scene.placer().sell_value == 20);
    CHECK(scene.cursor_shown());
    const auto* at = scene.app.world().get<transform2d::Position>(scene.cursor);
    CHECK(at->x == -10.0F);
    CHECK(at->y == 10.0F);
}

TEST_CASE("economy: a click elsewhere lets the selection go") {
    Scene scene;
    scene.touch(1, 3);
    scene.touch(1, 3);
    scene.touch(3, 3);
    CHECK(scene.seen["deselected"] == 1);
    CHECK(scene.placer().selected == 0U);
    CHECK(!scene.cursor_shown());
    CHECK(scene.wallet() == 20); // it built a second tower on the free cell it touched
}

TEST_CASE("economy: improving a tower pays, replaces it with the next one and selects that") {
    Scene scene;
    scene.touch(2, 2);
    scene.touch(2, 2);
    const std::uint64_t before = scene.placer().selected;
    scene.press("upgrade");
    REQUIRE(scene.asked->size() == 2U);
    CHECK(scene.asked->back().prefab == better);
    CHECK(scene.wallet() == 30); // 100 - 40 - 30
    CHECK(!scene.app.world().is_alive(Entity::from_link(before)));
    CHECK(scene.towers() == 1U);
    CHECK(scene.placer().selected != 0U);
    CHECK(scene.placer().selected != before);
}

TEST_CASE("economy: the improved tower is a final one: it can only be sold") {
    Scene scene;
    scene.touch(2, 2);
    scene.touch(2, 2);
    scene.press("upgrade");
    CHECK(scene.placer().upgrade_price == 0);
    CHECK(scene.placer().sell_value == 45);
    CHECK(scene.seen["final"] == 1);
}

TEST_CASE("economy: an improvement that costs too much changes nothing") {
    Scene scene;
    scene.app.world().get<economy::Wallet>(scene.purse)->amount = 50;
    scene.touch(2, 2);
    scene.touch(2, 2);
    scene.press("upgrade"); // 10 left, 30 needed
    CHECK(scene.asked->size() == 1U);
    CHECK(scene.wallet() == 10);
    CHECK(scene.seen["refused"] == 1);
    CHECK(scene.placer().selected != 0U);
    CHECK(scene.towers() == 1U);
}

TEST_CASE("economy: selling gives the refund back and frees the cell") {
    Scene scene;
    scene.touch(2, 2);
    scene.touch(2, 2);
    scene.press("sell");
    CHECK(scene.wallet() == 80); // 100 - 40 + 20
    CHECK(scene.towers() == 0U);
    CHECK(scene.placer().selected == 0U);
    CHECK(scene.seen["deselected"] == 1);
    CHECK(!scene.cursor_shown());
    scene.touch(2, 2); // free again
    CHECK(scene.asked->size() == 2U);
}

TEST_CASE("economy: a selected tower that is destroyed lets the selection go") {
    Scene scene;
    scene.touch(2, 2);
    scene.touch(2, 2);
    CHECK(scene.app.world().despawn(Entity::from_link(scene.placer().selected)));
    scene.play(3);
    CHECK(scene.placer().selected == 0U);
    CHECK(!scene.cursor_shown());
}

TEST_CASE("economy: a dead Bounty is paid once") {
    Scene scene;
    const Entity monster = scene.app.world().spawn();
    CHECK(scene.app.world().insert(monster, economy::Bounty{7, false}));
    health::Health life;
    life.amount = 1;
    life.max = 1;
    life.despawn_on_zero = false;
    CHECK(scene.app.world().insert(monster, life));
    scene.play(3);
    CHECK(scene.wallet() == 100); // alive: nothing yet
    CHECK(scene.app.world().insert(monster, health::Damage{1}));
    scene.play(5);
    CHECK(scene.wallet() == 107);
    scene.play(10);
    CHECK(scene.wallet() == 107);
}

TEST_CASE("economy: a Disabled placer does nothing") {
    Scene scene;
    CHECK(scene.app.world().insert(scene.cursor, transform2d::Disabled{}));
    scene.touch(1, 1);
    CHECK(scene.asked->empty());
    CHECK(scene.wallet() == 100);
}
