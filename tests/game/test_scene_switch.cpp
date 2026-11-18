// Changing scene while the game runs: the loader, persistent entities, the
// names that follow, and what makes a GoToScene fire.
#include "ge/app/app.hpp"
#include "ge/app/input.hpp"
#include "ge/game/game.hpp"
#include "ge/game/runtime.hpp"
#include "ge/game/scenario.hpp"
#include "ge/mod/collision2d/collision2d.gen.hpp"
#include "ge/mod/scene_switch/scene_switch.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "../support/mini_test.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <string>

using ge::game::FileReader;
using ge::game::ModuleEntry;

namespace {

namespace scene_switch = ge::mod::scene_switch;
namespace transform2d = ge::mod::transform2d;

const std::array<ModuleEntry, 3> linked = {{
    {&transform2d::info, &transform2d::install},
    {&ge::mod::collision2d::info, &ge::mod::collision2d::install},
    {&scene_switch::info, &scene_switch::install},
}};

using Files = std::map<std::string, std::string>;

/// A menu with a Start button (an action), a level with a door (touching), and
/// a credits screen that follows after a delay. The score stays.
Files sample() {
    return {
        {"g/game.json", R"({
            "schema_version": 1, "name": "levels",
            "modules": ["transform2d", "collision2d", "scene_switch"],
            "lists": {"layer": ["player", "door"]},
            "input": "input.json",
            "prefabs": ["prefabs/door.json"],
            "scenes": ["scenes/menu.json", "scenes/level.json", "scenes/credits.json"],
            "start_scene": "menu",
            "simulation": {"tick_rate": 10},
        })"},
        {"g/input.json", R"({"schema_version": 1, "bindings": [
            {"action": "start", "sources": {"key:return": 1}}]})"},
        {"g/prefabs/door.json", R"({"schema_version": 1, "name": "door", "components": {
            "transform2d.Position": {"x": 100},
            "collision2d.Collider": {"width": 10, "height": 10, "layer": ["door"]},
            "collision2d.Contacts": {},
            "scene_switch.GoToScene": {"scene": "credits", "touch_layers": ["player"]}}})"},
        {"g/scenes/menu.json", R"({"schema_version": 1, "name": "menu", "entities": [
            {"name": "score", "components": {"transform2d.Position": {"x": 7},
                                            "scene_switch.Persistent": {}}},
            {"name": "start_button", "components": {
                "scene_switch.GoToScene": {"scene": "level", "use_action": true, "action": "start"}}},
            {"name": "menu_prop", "components": {"transform2d.Position": {"x": 1}}},
        ]})"},
        {"g/scenes/level.json", R"({"schema_version": 1, "name": "level", "entities": [
            {"name": "hero", "components": {
                "transform2d.Position": {"x": 0}, "transform2d.Velocity": {"x": 60},
                "collision2d.Collider": {"width": 10, "height": 10, "layer": ["player"]},
                "collision2d.Contacts": {}}},
            {"name": "exit", "prefab": "door"},
        ]})"},
        {"g/scenes/credits.json", R"({"schema_version": 1, "name": "credits", "entities": [
            {"name": "roll", "components": {"scene_switch.GoToScene": {
                "scene": "menu", "after_ticks": 5}}},
        ]})"},
    };
}

ge::game::LoadedGame load(const Files& files) {
    const FileReader reader = [&files](const std::string& path) -> std::optional<std::string> {
        const auto found = files.find(path);
        if (found == files.end()) {
            return std::nullopt;
        }
        return found->second;
    };
    return ge::game::load_game("g/game.json", reader, ge::game::catalog_of(linked));
}

bool mentions(const ge::game::LoadedGame& loaded, const char* fragment) {
    return std::ranges::any_of(loaded.diagnostics, [fragment](const auto& diagnostic) {
        return diagnostic.to_string().find(fragment) != std::string::npos;
    });
}

ge::game::LoadedGame load_changed(const char* file, const char* from, const char* to) {
    Files files = sample();
    std::string& text = files[file];
    const std::size_t at = text.find(from);
    text = at == std::string::npos ? std::string("TEST SETUP: not found: ") + from
                                   : text.replace(at, std::string(from).size(), to);
    return load(files);
}

bool alive(const ge::app::App& app, const char* name) {
    const auto* names = app.world().resource<ge::app::EntityNames>();
    return names != nullptr && app.world().is_alive(names->find(name));
}

std::uint32_t scene_of(const ge::app::App& app) {
    const auto* current = app.world().resource<ge::app::CurrentScene>();
    return current != nullptr ? current->index : 999U;
}

} // namespace

TEST_CASE("scenes: scenes, actions and layers are named in the files that use them") {
    const auto loaded = load(sample());
    for (const auto& diagnostic : loaded.diagnostics) {
        CHECK(diagnostic.to_string().empty());
    }
    REQUIRE(loaded.ok());
    CHECK(loaded.game.ref_names("scene").size() == 3U);
    REQUIRE(loaded.game.ref_names("action").size() == 1U);
    CHECK(loaded.game.ref_names("action")[0] == "start"); // from input.json
}

TEST_CASE("scenes: an unknown scene or action says what exists") {
    const auto scene = load_changed("g/prefabs/door.json", "\"credits\"", "\"credit\"");
    CHECK(mentions(scene, "no scene is named \"credit\""));
    CHECK(mentions(scene, "did you mean \"credits\""));
    const auto action = load_changed("g/scenes/menu.json", "\"start\"", "\"go\"");
    CHECK(mentions(action, "no action is named \"go\""));
    CHECK(mentions(action, "expected one of: start"));
}

TEST_CASE("scenes: a pressed action takes the game to another scene, keeping what is persistent") {
    const auto loaded = load(sample());
    REQUIRE(loaded.ok());
    ge::app::App app;
    REQUIRE(ge::game::setup_game(app, loaded.game, linked).ok());
    CHECK(scene_of(app) == 0U);
    CHECK(alive(app, "menu_prop"));
    app.run_frames(3);
    CHECK(scene_of(app) == 0U); // nothing pressed: still the menu

    auto* live = app.world().resource<ge::app::LiveInput>();
    REQUIRE(live != nullptr);
    live->set("start", 1.0F);
    app.run_frames(1);
    CHECK(scene_of(app) == 1U);
    CHECK(alive(app, "hero"));
    CHECK(!alive(app, "menu_prop")); // the menu is gone...
    CHECK(!alive(app, "start_button"));
    CHECK(alive(app, "score")); // ...except what stays
    const auto* names = app.world().resource<ge::app::EntityNames>();
    const auto* score = app.world().get<transform2d::Position>(names->find("score"));
    REQUIRE(score != nullptr);
    CHECK(score->x == 7.0F);
}

TEST_CASE("scenes: touching a door and a timer also change the scene") {
    const auto loaded = load(sample());
    REQUIRE(loaded.ok());
    ge::app::App app;
    REQUIRE(ge::game::setup_game(app, loaded.game, linked).ok());
    app.world().resource<ge::app::LiveInput>()->set("start", 1.0F);
    app.run_frames(1);
    REQUIRE(scene_of(app) == 1U);
    // At 10 ticks per second the hero (60 units/s) walks 6 units per tick towards the door at x =
    // 100; the boxes are 10 wide, so they overlap once the hero passes x = 90.
    app.run_frames(15);
    CHECK(scene_of(app) == 1U); // x = 90: the boxes only share an edge
    app.run_frames(1);
    CHECK(scene_of(app) == 2U); // overlapping: the door sent the game to the credits
    CHECK(alive(app, "roll"));
    CHECK(!alive(app, "hero"));
    app.run_frames(6);
    CHECK(scene_of(app) == 0U); // the credits' timer (5 ticks) leads back to the menu
    CHECK(alive(app, "start_button"));
    CHECK(alive(app, "score"));
}

TEST_CASE("scenes: a scenario follows the game from scene to scene") {
    Files files = sample();
    std::string& manifest = files["g/game.json"];
    manifest.replace(manifest.find(R"("start_scene": "menu",)"), 0,
                     R"("scenarios": ["scenarios/play.json"],)");
    files["g/scenarios/play.json"] = R"({"schema_version": 1, "name": "play", "ticks": 40,
        "inputs": [{"tick": 2, "action": "start"}, {"tick": 3, "action": "start", "value": 0}],
        "expect": [
            {"at": 2, "check": "scene is menu"},
            {"at": 4, "check": "scene is level"},
            {"at": 4, "check": "hero alive"},
            {"at": 23, "check": "scene is credits"},
            {"at": 23, "check": "roll alive"},
            "scene is menu"]})";
    const auto loaded = load(files);
    for (const auto& diagnostic : loaded.diagnostics) {
        CHECK(diagnostic.to_string().empty());
    }
    REQUIRE(loaded.ok());
    const auto result = ge::game::run_scenario(loaded.game, loaded.game.scenarios[0], linked);
    for (const std::string& failure : result.failures) {
        CHECK(failure == "");
    }
    CHECK(result.passed());

    // A scene that does not exist is reported when the game is loaded, with a suggestion.
    files["g/scenarios/play.json"] = R"({"schema_version": 1, "name": "play", "ticks": 5,
        "expect": ["scene is levels"]})";
    CHECK(mentions(load(files), "no scene is named \"levels\" (did you mean \"level\"?)"));
    // And a scenario that ends elsewhere fails with the scene it found.
    files["g/scenarios/play.json"] = R"({"schema_version": 1, "name": "play", "ticks": 5,
        "expect": ["scene is credits"]})";
    const auto wrong = load(files);
    REQUIRE(wrong.ok());
    const auto failed = ge::game::run_scenario(wrong.game, wrong.game.scenarios[0], linked);
    CHECK(!failed.passed());
    CHECK(!failed.failures.empty());
}

TEST_CASE("scenes: without a loader a request is ignored, and a bad index changes nothing") {
    ge::app::App app;
    app.world().resource<ge::app::SceneRequest>()->scene = 1U;
    app.run_frames(1); // no loader: nothing happens, nothing breaks
    CHECK(app.world().resource<ge::app::SceneRequest>()->scene.has_value());

    const auto loaded = load(sample());
    REQUIRE(loaded.ok());
    ge::app::App running;
    REQUIRE(ge::game::setup_game(running, loaded.game, linked).ok());
    ge::game::change_scene(running, loaded.game, 42U);
    CHECK(scene_of(running) == 0U);
    CHECK(alive(running, "menu_prop"));
}
