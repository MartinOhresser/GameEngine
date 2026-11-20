#include "ge/game/game.hpp"
#include "ge/game/runtime.hpp"
#include "ge/game/scenario.hpp"
#include "ge/mod/action_control/action_control.gen.hpp"
#include "ge/mod/host/host.gen.hpp"
#include "ge/mod/lifetime/lifetime.gen.hpp"
#include "ge/mod/sprite2d/sprite2d.gen.hpp"
#include "ge/mod/tag/tag.gen.hpp"
#include "ge/mod/text2d/text2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"
#include "ge/mod/ui/ui.gen.hpp"

#include "../support/mini_test.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <string>

using ge::game::Check;
using ge::game::CheckKind;
using ge::game::Compare;
using ge::game::FileReader;
using ge::game::ModuleEntry;

namespace {

const std::array<ModuleEntry, 8> linked = {{
    {&ge::mod::transform2d::info, &ge::mod::transform2d::install},
    {&ge::mod::lifetime::info, &ge::mod::lifetime::install},
    {&ge::mod::action_control::info, &ge::mod::action_control::install},
    {&ge::mod::host::info, &ge::mod::host::install},
    {&ge::mod::sprite2d::info, &ge::mod::sprite2d::install},
    {&ge::mod::text2d::info, &ge::mod::text2d::install},
    {&ge::mod::tag::info, &ge::mod::tag::install},
    {&ge::mod::ui::info, &ge::mod::ui::install},
}};

using Files = std::map<std::string, std::string>;

FileReader reader_of(const Files& files) {
    return [&files](const std::string& path) -> std::optional<std::string> {
        const auto found = files.find(path);
        if (found == files.end()) {
            return std::nullopt;
        }
        return found->second;
    };
}

Files sample() {
    return {
        {"g/game.json", R"({
            "schema_version": 1, "name": "demo",
            "modules": ["transform2d", "lifetime", "action_control"],
            "prefabs": ["pilot.json"], "scenes": ["main.json"],
            "scenarios": ["run_right.json"],
            "start_scene": "main"
        })"},
        {"g/pilot.json", R"({
            "schema_version": 1, "name": "pilot",
            "components": {
                "transform2d.Position": {}, "transform2d.Velocity": {},
                "action_control.Controlled": {"speed": 60}
            }
        })"},
        {"g/main.json", R"({
            "schema_version": 1, "name": "main",
            "entities": [
                {"name": "hero", "prefab": "pilot"},
                {"name": "spark", "components": {"lifetime.Lifetime": {"ticks": 10}}}
            ]
        })"},
        {"g/run_right.json", R"({
            "schema_version": 1, "name": "run_right", "ticks": 60,
            "inputs": [
                {"tick": 0, "action": "move_x", "value": 1},
                {"tick": 30, "action": "move_x", "value": 0}
            ],
            "expect": [
                {"at": 30, "check": "hero.transform2d.Position.x == 30"},
                "hero.transform2d.Position.x == 30",
                "hero.transform2d.Velocity.x == 0",
                {"at": 9, "check": "spark alive"},
                "spark gone",
                "entities == 1"
            ]
        })"},
    };
}

ge::game::Catalog catalog() {
    return ge::game::catalog_of(linked);
}

ge::game::LoadedGame load(const Files& files) {
    return ge::game::load_game("g/game.json", reader_of(files), catalog());
}

bool mentions(const ge::game::LoadedGame& loaded, const char* fragment) {
    return std::ranges::any_of(loaded.diagnostics, [fragment](const auto& diagnostic) {
        return diagnostic.to_string().find(fragment) != std::string::npos;
    });
}

ge::game::LoadedGame load_changed(const char* from, const char* to) {
    Files files = sample();
    std::string& text = files["g/run_right.json"];
    const std::size_t at = text.find(from);
    if (at != std::string::npos) {
        text.replace(at, std::string(from).size(), to);
    } else {
        text = std::string("TEST SETUP: text not found: ") + from;
    }
    return load(files);
}

std::string all_failures(const ge::game::ScenarioReport& report) {
    std::string text;
    for (const std::string& line : report.failures) {
        text += line + "\n";
    }
    return text;
}

bool has(const std::string& text, const char* fragment) {
    return text.find(fragment) != std::string::npos;
}

} // namespace

TEST_CASE("check: the forms of a check are understood") {
    Check check;
    CHECK(ge::game::parse_check("hero.transform2d.Position.x > 100", check).empty());
    CHECK(check.kind == CheckKind::Field);
    CHECK(check.entity == "hero");
    CHECK(check.component == "transform2d.Position");
    CHECK(check.field == "x");
    CHECK(check.compare == Compare::Gt);
    CHECK(check.value.as_int() == 100);
    CHECK(check.text == "hero.transform2d.Position.x > 100");

    CHECK(ge::game::parse_check("hero.m.C.flag == true", check).empty());
    CHECK(check.value.as_bool());
    CHECK(ge::game::parse_check("  entities   <=   3 ", check).empty());
    CHECK(check.kind == CheckKind::EntityCount);
    CHECK(check.compare == Compare::Le);
    CHECK(ge::game::parse_check("hero alive", check).empty());
    CHECK(check.kind == CheckKind::Alive);
    CHECK(ge::game::parse_check("spark gone", check).empty());
    CHECK(check.kind == CheckKind::Gone);
    CHECK(ge::game::parse_check("scene is credits", check).empty());
    CHECK(check.kind == CheckKind::Scene);
    CHECK(check.entity == "credits");
    CHECK(ge::game::parse_check("game quits", check).empty());
    CHECK(check.kind == CheckKind::Quit);
    CHECK(ge::game::parse_check("host calls play", check).empty());
    CHECK(check.kind == CheckKind::HostCall);
    CHECK(check.entity == "play");
    CHECK(ge::game::parse_check("fingerprint == 5f4962bd950cf9b3", check).empty());
    CHECK(check.kind == CheckKind::Fingerprint);
    CHECK(check.fingerprint == 0x5f4962bd950cf9b3ULL);
    CHECK(ge::game::parse_check("fingerprint != ff", check).empty());
    CHECK(check.compare == Compare::Ne);
}

TEST_CASE("check: badly written checks say what was expected") {
    Check check;
    CHECK(ge::game::parse_check("", check).find("expected") != std::string::npos);
    CHECK(ge::game::parse_check("hero is cool", check).find("unknown comparison") !=
          std::string::npos);
    CHECK(ge::game::parse_check("hero.x > 1", check).find("entity.module.Component.field") !=
          std::string::npos);
    CHECK(ge::game::parse_check("hero.m.C.f > ten", check).find("not a number") !=
          std::string::npos);
    CHECK(ge::game::parse_check("entities > 1.5", check).find("integer") != std::string::npos);
    CHECK(ge::game::parse_check("fingerprint > 1", check).find("fingerprint check") !=
          std::string::npos);
    CHECK(ge::game::parse_check("fingerprint == xyz", check).find("hexadecimal") !=
          std::string::npos);
    CHECK(ge::game::parse_check("fingerprint == 00112233445566778", check).find("hexadecimal") !=
          std::string::npos);
    CHECK(ge::game::parse_check("a b c d", check).find("expected") != std::string::npos);
    CHECK(ge::game::parse_check("hero.m..f > 1", check).find("entity.module.Component.field") !=
          std::string::npos);
}

TEST_CASE("scenario: a valid scenario loads with its inputs and checks") {
    const auto loaded = load(sample());
    for (const auto& diagnostic : loaded.diagnostics) {
        CHECK(diagnostic.to_string().empty());
    }
    REQUIRE(loaded.ok());
    REQUIRE(loaded.game.scenarios.size() == 1U);
    const auto& scenario = loaded.game.scenarios[0];
    CHECK(scenario.name == "run_right");
    CHECK(scenario.ticks == 60U);
    CHECK(scenario.inputs.size() == 2U);
    CHECK(scenario.inputs[1].tick == 30U);
    CHECK(scenario.inputs[1].value == 0.0F);
    REQUIRE(scenario.checks.size() == 6U);
    CHECK(scenario.checks[0].at == 30U);
    CHECK(scenario.checks[1].at == 60U); // a bare string checks at the end
    CHECK(loaded.game.find_scenario("run_right") != nullptr);
    CHECK(loaded.game.find_scenario("nope") == nullptr);
}

TEST_CASE("scenario: file problems are reported with their place") {
    CHECK(mentions(load_changed("\"ticks\": 60", "\"ticks\": 0"),
                   "expected a number of ticks between 1 and"));
    CHECK(mentions(load_changed("\"ticks\": 60,", ""), "ticks: expected a number of ticks"));
    CHECK(mentions(load_changed("\"ticks\": 60", "\"ticks\": 60.5"),
                   "ticks: expected a number of ticks"));
    CHECK(mentions(load_changed("\"schema_version\": 1,", ""),
                   "missing required key \"schema_version\""));
    CHECK(mentions(load_changed("\"schema_version\": 1", "\"schema_version\": 2"),
                   "only schema version 1"));
    CHECK(
        mentions(load_changed("\"name\": \"run_right\"", "\"name\": \"Run\""), "lower_snake_case"));
    CHECK(mentions(load_changed("\"ticks\": 60,", "\"ticks\": 60, \"speed\": 2,"), "unknown key"));
    CHECK(mentions(load_changed("\"inputs\": [", "\"inputs\": 5, \"x\": ["),
                   "inputs: expected an array"));
    CHECK(mentions(load_changed("{\"tick\": 30, \"action\": \"move_x\", \"value\": 0}",
                                "{\"tick\": 99, \"action\": \"move_x\"}"),
                   "inputs[1].tick: the scenario ends at tick 60"));
    CHECK(mentions(load_changed("{\"tick\": 30, \"action\": \"move_x\", \"value\": 0}",
                                "{\"tick\": -1, \"action\": \"move_x\"}"),
                   "inputs[1].tick: expected a whole number of ticks"));
    CHECK(mentions(
        load_changed("\"action\": \"move_x\", \"value\": 1", "\"action\": \"Move\", \"value\": 1"),
        "lower_snake_case"));
    CHECK(mentions(load_changed("\"value\": 1}", "\"value\": \"high\"}"),
                   "expected a number, found a string"));
    CHECK(mentions(load_changed("\"value\": 1}", "\"value\": 1, \"force\": 2}"), "unknown key"));
    CHECK(mentions(load_changed("{\"tick\": 0, \"action\": \"move_x\", \"value\": 1}", "7"),
                   "expected an object"));
}

TEST_CASE("scenario: the checks are validated against the game") {
    CHECK(mentions(load_changed("spark gone", "sparkle gone"),
                   "expect[4]: the scene \"main\" has no entity named \"sparkle\" (did you "
                   "mean \"spark\"?)"));
    CHECK(mentions(
        load_changed("hero.transform2d.Velocity.x == 0", "hero.transform2d.Velocty.x == 0"),
        "unknown component \"transform2d.Velocty\""));
    CHECK(mentions(load_changed("hero.transform2d.Velocity.x == 0", "hero.physics.Body.x == 0"),
                   "unknown component \"physics.Body\""));
    CHECK(mentions(
        load_changed("hero.transform2d.Velocity.x == 0", "hero.transform2d.Velocity.z == 0"),
        "has no field \"z\" (expected one of: x, y)"));
    CHECK(mentions(
        load_changed("hero.transform2d.Velocity.x == 0", "hero.transform2d.Velocity.x == true"),
        "a numeric field is compared to a number"));
    CHECK(mentions(load_changed("\"entities == 1\"", "\"entities == one\""),
                   "cannot read the check"));
    CHECK(mentions(load_changed("{\"at\": 9, \"check\": \"spark alive\"}",
                                "{\"at\": 61, \"check\": \"spark alive\"}"),
                   "expected a tick between 0 and 60"));
    CHECK(mentions(load_changed("{\"at\": 9, \"check\": \"spark alive\"}",
                                "{\"at\": 9, \"when\": \"spark alive\"}"),
                   "unknown key"));
    CHECK(mentions(load_changed("{\"at\": 9, \"check\": \"spark alive\"}", "{\"at\": 9}"),
                   "missing required key \"check\""));
    CHECK(mentions(load_changed("\"entities == 1\"", "12"), "expected an object"));
}

TEST_CASE("scenario: names are unique and files must exist") {
    Files files = sample();
    files["g/game.json"] = R"({
        "schema_version": 1, "name": "demo",
        "modules": ["transform2d", "lifetime", "action_control"],
        "prefabs": ["pilot.json"], "scenes": ["main.json"],
        "scenarios": ["run_right.json", "again.json", "gone.json"],
        "start_scene": "main"
    })";
    files["g/again.json"] = files["g/run_right.json"];
    const auto loaded = load(files);
    CHECK(mentions(loaded, "another scenario is already named \"run_right\""));
    CHECK(mentions(loaded, "scenarios[2]: cannot read the file \"g/gone.json\""));
}

TEST_CASE("scenario: running it plays the inputs and passes when the game does what it says") {
    const auto loaded = load(sample());
    REQUIRE(loaded.ok());
    const auto report = ge::game::run_scenario(loaded.game, loaded.game.scenarios[0], linked);
    CHECK(all_failures(report).empty());
    CHECK(report.passed());
    CHECK(report.name == "run_right");
    // Running it again gives the same final state.
    CHECK(ge::game::run_scenario(loaded.game, loaded.game.scenarios[0], linked).fingerprint ==
          report.fingerprint);
}

TEST_CASE("scenario: failures say the tick, the check and the value found") {
    Files files = sample();
    files["g/run_right.json"] = R"({
        "schema_version": 1, "name": "run_right", "ticks": 20,
        "inputs": [{"tick": 0, "action": "move_x", "value": 1}],
        "expect": [
            "hero.transform2d.Position.x > 100",
            {"at": 5, "check": "hero.transform2d.Position.x < 3"},
            {"at": 5, "check": "hero.transform2d.Position.x != 6"},
            {"at": 5, "check": "hero.transform2d.Position.x >= 5"},
            {"at": 5, "check": "hero.transform2d.Position.x <= 5"},
            {"at": 5, "check": "hero.transform2d.Position.x == 5"},
            "spark alive",
            "hero gone",
            "entities == 5",
            "fingerprint == 1",
            "fingerprint != 1"
        ]
    })";
    const auto loaded = load(files);
    REQUIRE(loaded.ok());
    const auto report = ge::game::run_scenario(loaded.game, loaded.game.scenarios[0], linked);
    const std::string text = all_failures(report);
    CHECK(!report.passed());
    CHECK(has(text, "tick 20: \"hero.transform2d.Position.x > 100\" is false (the value is 20)"));
    CHECK(has(text, "tick 5: \"hero.transform2d.Position.x < 3\" is false (the value is 5)"));
    CHECK(has(text, "tick 20: \"spark alive\" is false (\"spark\" no longer exists)"));
    CHECK(has(text, "tick 20: \"hero gone\" is false (\"hero\" still exists)"));
    CHECK(has(text, "tick 20: \"entities == 5\" is false (there are 1 entities)"));
    CHECK(has(text, "tick 20: \"fingerprint == 1\" is false (the fingerprint is "));
    // Checks that hold are not reported.
    CHECK(!has(text, "!= 6"));
    CHECK(!has(text, ">= 5"));
    CHECK(!has(text, "<= 5"));
    CHECK(!has(text, "Position.x == 5"));
    CHECK(!has(text, "fingerprint != 1"));
}

TEST_CASE("scenario: a check on an entity that is gone, or a component it lacks, fails clearly") {
    Files files = sample();
    files["g/run_right.json"] = R"({
        "schema_version": 1, "name": "run_right", "ticks": 20,
        "expect": [
            "spark.lifetime.Lifetime.ticks > 0",
            "hero.lifetime.Lifetime.ticks > 0"
        ]
    })";
    const auto loaded = load(files);
    REQUIRE(loaded.ok());
    const std::string text =
        all_failures(ge::game::run_scenario(loaded.game, loaded.game.scenarios[0], linked));
    CHECK(has(text, "\"spark\" no longer exists"));
    CHECK(has(text, "\"hero\" has no lifetime.Lifetime"));
}

TEST_CASE("scenario: a fingerprint written down from a run holds on the next run") {
    const auto loaded = load(sample());
    REQUIRE(loaded.ok());
    const auto first = ge::game::run_scenario(loaded.game, loaded.game.scenarios[0], linked);
    const std::string fingerprint = ge::game::format_fingerprint(first.fingerprint);
    Files files = sample();
    files["g/run_right.json"] =
        std::string(R"({"schema_version": 1, "name": "run_right", "ticks": 60,
        "inputs": [{"tick": 0, "action": "move_x", "value": 1}, {"tick": 30, "action": "move_x", "value": 0}],
        "expect": ["fingerprint == )") +
        fingerprint + "\"]}";
    const auto again = load(files);
    REQUIRE(again.ok());
    CHECK(ge::game::run_scenario(again.game, again.game.scenarios[0], linked).passed());
}

TEST_CASE("scenario: a game that cannot start fails the scenario") {
    const auto loaded = load(sample());
    REQUIRE(loaded.ok());
    const std::array<ModuleEntry, 1> only_transform = {linked[0]};
    const auto report =
        ge::game::run_scenario(loaded.game, loaded.game.scenarios[0], only_transform);
    CHECK(!report.passed());
    CHECK(has(all_failures(report), "not linked into this program"));
}

TEST_CASE("scenario: a scenario can start another scene than the game's") {
    Files files = sample();
    files["g/game.json"] = R"({
        "schema_version": 1, "name": "demo",
        "modules": ["transform2d", "lifetime", "action_control"],
        "prefabs": ["pilot.json"], "scenes": ["main.json", "arena.json"],
        "scenarios": ["in_arena.json"], "start_scene": "main"
    })";
    files["g/arena.json"] = R"({"schema_version": 1, "name": "arena", "entities": [
        {"name": "boss", "prefab": "pilot"}]})";
    files["g/in_arena.json"] = R"({"schema_version": 1, "name": "in_arena", "scene": "arena",
        "ticks": 5, "expect": ["boss alive", "entities == 1"]})";
    const auto loaded = load(files);
    for (const auto& diagnostic : loaded.diagnostics) {
        CHECK(diagnostic.to_string().empty());
    }
    REQUIRE(loaded.ok());
    CHECK(loaded.game.scenarios[0].scene == "arena");
    CHECK(ge::game::run_scenario(loaded.game, loaded.game.scenarios[0], linked).passed());

    // The entities it may name are the ones of the game's scenes; others are unknown.
    files["g/in_arena.json"] = R"({"schema_version": 1, "name": "in_arena", "scene": "arena",
        "ticks": 5, "expect": ["nobody alive"]})";
    CHECK(mentions(load(files), "the scene \"arena\" has no entity named \"nobody\""));
    // An unknown scene is reported with a suggestion.
    files["g/in_arena.json"] = R"({"schema_version": 1, "name": "in_arena", "scene": "aren",
        "ticks": 5})";
    CHECK(mentions(load(files), "scene: no scene is named \"aren\" (did you mean \"arena\"?)"));
}

TEST_CASE("scenario: a game can be checked to quit and to call the host") {
    Files files = sample();
    files["g/game.json"] = R"({
        "schema_version": 1, "name": "demo",
        "modules": ["transform2d", "host"],
        "lists": {"action": ["leave", "go"], "host_command": ["play", "export"]},
        "scenes": ["main.json"], "scenarios": ["leaves.json", "stays.json", "calls.json"],
        "start_scene": "main"
    })";
    files["g/main.json"] = R"({"schema_version": 1, "name": "main", "entities": [
        {"name": "quit", "components": {"host.Quit": {"action": "leave"}}},
        {"name": "button", "components": {"host.Call": {"command": "export", "action": "go",
                                                          "argument": 3}}}]})";
    files["g/leaves.json"] = R"({"schema_version": 1, "name": "leaves", "ticks": 6,
        "inputs": [{"tick": 2, "action": "leave", "value": 1}],
        "expect": [{"at": 2, "check": "game quits"}, "game quits"]})";
    files["g/stays.json"] = R"({"schema_version": 1, "name": "stays", "ticks": 6,
        "expect": ["game quits"]})";
    files["g/calls.json"] = R"({"schema_version": 1, "name": "calls", "ticks": 6,
        "inputs": [{"tick": 2, "action": "go", "value": 1}],
        "expect": ["host calls export", {"at": 1, "check": "host calls export"}]})";
    const auto loaded = load(files);
    for (const auto& diagnostic : loaded.diagnostics) {
        CHECK(diagnostic.to_string().empty());
    }
    REQUIRE(loaded.ok());
    const auto leaves = ge::game::run_scenario(loaded.game, loaded.game.scenarios[0], linked);
    CHECK(has(all_failures(leaves), "tick 2")); // not yet at tick 2: pressed after it
    const auto stays = ge::game::run_scenario(loaded.game, loaded.game.scenarios[1], linked);
    CHECK(has(all_failures(stays), "the game did not ask to quit"));
    const auto calls = ge::game::run_scenario(loaded.game, loaded.game.scenarios[2], linked);
    CHECK(has(all_failures(calls), "tick 1"));
    CHECK(!has(all_failures(calls), "tick 6"));

    // A command that is not in the list is reported, with a suggestion.
    files["g/calls.json"] = R"({"schema_version": 1, "name": "calls", "ticks": 6,
        "expect": ["host calls exprot"]})";
    CHECK(mentions(load(files), "no host command is named \"exprot\""));
}

TEST_CASE("scenario: a scenario fills a run-time list, selects a row and a host command gets it") {
    Files files;
    files["g/game.json"] = R"({
        "schema_version": 1, "name": "picker",
        "modules": ["transform2d", "sprite2d", "text2d", "tag", "ui", "host"],
        "lists": {"runtime_list": ["games"], "menu": ["main"],
                  "action": ["down", "choose", "play_it"], "host_command": ["play"],
                  "tag": [], "group": []},
        "scenes": ["main.json"], "scenarios": ["pick.json"], "start_scene": "main"
    })";
    files["g/main.json"] = R"({"schema_version": 1, "name": "main", "entities": [
        {"name": "controller", "components": {"ui.Menu": {"group": "main", "down": "down",
                                                           "accept": "choose"}}},
        {"name": "pages", "components": {"ui.Pager": {"list": "games", "menu": "main",
                                                      "page_size": 2}}},
        {"name": "row0", "components": {"transform2d.Position": {"y": 0}, "sprite2d.Sprite": {},
            "text2d.Text": {}, "text2d.Dynamic": {}, "ui.Row": {"list": "games", "row": 0},
            "ui.Button": {"menu": "main", "order": 0, "action": "play_it"}}},
        {"name": "row1", "components": {"transform2d.Position": {"y": 40}, "sprite2d.Sprite": {},
            "text2d.Text": {}, "text2d.Dynamic": {}, "ui.Row": {"list": "games", "row": 1},
            "ui.Button": {"menu": "main", "order": 1, "action": "play_it"}}},
        {"name": "go", "components": {"host.Call": {"command": "play", "action": "play_it",
                                                      "list": "games"}}}]})";
    files["g/pick.json"] = R"({"schema_version": 1, "name": "pick", "ticks": 12,
        "lists": {"games": ["Alpha", "Beta", "Gamma"]},
        "inputs": [{"tick": 2, "action": "down", "value": 1}, {"tick": 3, "action": "down", "value": 0},
                   {"tick": 5, "action": "choose", "value": 1}, {"tick": 6, "action": "choose", "value": 0}],
        "expect": [{"at": 1, "check": "list games selected == 0"},
                   {"at": 4, "check": "list games selected == 1"},
                   {"at": 4, "check": "row1.ui.Row.item == 1"},
                   "controller.ui.Menu.focused == 1", "host calls play"]})";
    const auto loaded = load(files);
    for (const auto& diagnostic : loaded.diagnostics) {
        CHECK(diagnostic.to_string().empty());
    }
    REQUIRE(loaded.ok());
    const auto report = ge::game::run_scenario(loaded.game, loaded.game.scenarios[0], linked);
    CHECK(all_failures(report).empty());

    // A list the game never declared is reported, in the scenario and in its checks.
    files["g/pick.json"] = R"({"schema_version": 1, "name": "pick", "ticks": 3,
        "lists": {"gamez": ["Alpha"]}, "expect": ["list gamez selected == 0"]})";
    CHECK(mentions(load(files), "no run-time list is named \"gamez\""));
}

namespace {

/// A game whose window follows the screen, with a bar stuck to the bottom edge of the window.
Files phone_sample(const std::string& screen) {
    return {
        {"g/game.json", R"({
            "schema_version": 1, "name": "phone",
            "modules": ["transform2d", "sprite2d", "text2d", "tag", "ui"],
            "window": {"width": 480, "height": 720, "adapt": true},
            "scenes": ["main.json"], "scenarios": ["look.json"], "start_scene": "main"
        })"},
        {"g/main.json", R"({
            "schema_version": 1, "name": "main",
            "entities": [{"name": "bar", "components": {
                "transform2d.Position": {}, "ui.Anchor": {"anchor_y": 1}}}]
        })"},
        {"g/look.json", R"({"schema_version": 1, "name": "look", "ticks": 3)" + screen + R"(,
            "expect": ["bar.transform2d.Position.y == 960"]})"},
    };
}

} // namespace

TEST_CASE("scenario: a screen makes the window follow its shape, as a phone's would") {
    // A screen of 320 x 640 (1 : 2) is taller than the 480 x 720 window: the area is 480 x 960.
    const auto tall = load(phone_sample(R"(, "screen": {"width": 320, "height": 640})"));
    for (const auto& diagnostic : tall.diagnostics) {
        CHECK(diagnostic.to_string().empty());
    }
    REQUIRE(tall.ok());
    CHECK(tall.game.scenarios[0].screen_width == 320);
    CHECK(tall.game.scenarios[0].screen_height == 640);
    CHECK(ge::game::run_scenario(tall.game, tall.game.scenarios[0], linked).passed());
    // Without a screen the area is the window's own: the bar is at 720, not 960.
    const auto plain = load(phone_sample(""));
    REQUIRE(plain.ok());
    CHECK(plain.game.scenarios[0].screen_width == 0);
    CHECK(!ge::game::run_scenario(plain.game, plain.game.scenarios[0], linked).passed());
}

TEST_CASE("scenario: a screen without sense is refused") {
    CHECK(mentions(load(phone_sample(R"(, "screen": {"width": 0, "height": 640})")),
                   "screen.width: expected a number of pixels"));
    CHECK(mentions(load(phone_sample(R"(, "screen": {"width": 320})")),
                   "screen.height: expected a number of pixels"));
    CHECK(mentions(load(phone_sample(R"(, "screen": {"width": 320, "height": 640, "dpi": 2})")),
                   "unknown key"));
    CHECK(mentions(load(phone_sample(R"(, "screen": 5)")), "expected an object"));
}
