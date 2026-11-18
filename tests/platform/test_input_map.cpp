#include "ge/platform/input_map.hpp"

#include "mini_test.hpp"

#include <algorithm>

using ge::app::LiveInput;
using ge::platform::RawState;

namespace {

constexpr const char* arrows_file = R"({
  // Arrows or WASD move the ship.
  "schema_version": 1,
  "bindings": [
    { "action": "move_x", "sources": { "key:right": 1, "key:d": 1, "key:left": -1, "key:a": -1 } },
    { "action": "fire", "sources": { "key:space": 1, "mouse:left": 1 } },
  ],
})";

bool mentions(const ge::platform::InputMapResult& result, const std::string& text) {
    return std::ranges::any_of(result.problems, [&text](const auto& problem) {
        return problem.to_string().find(text) != std::string::npos;
    });
}

} // namespace

TEST_CASE("input map: a file of bindings is read") {
    const auto result = ge::platform::parse_input_map(arrows_file, "input.json");
    CHECK(result.ok());
    CHECK(result.map.bindings().size() == 6U);
    CHECK(result.map.bindings()[0].action == "move_x");
    CHECK(result.map.bindings()[0].source == "key:right");
    CHECK(result.map.bindings()[2].scale == -1.0F);
}

TEST_CASE("input map: raw values become actions, an axis from two keys") {
    const auto result = ge::platform::parse_input_map(arrows_file, "input.json");
    CHECK(result.ok());
    RawState raw;
    LiveInput actions;
    result.map.apply(raw, actions);
    CHECK(actions.value("move_x") == 0.0F);
    CHECK(actions.value("fire") == 0.0F);

    raw.set("key:right", 1.0F);
    result.map.apply(raw, actions);
    CHECK(actions.value("move_x") == 1.0F);

    raw.set("key:left", 1.0F); // both pressed: they cancel
    result.map.apply(raw, actions);
    CHECK(actions.value("move_x") == 0.0F);

    raw.set("key:right", 0.0F);
    result.map.apply(raw, actions);
    CHECK(actions.value("move_x") == -1.0F);
}

TEST_CASE("input map: the pointer's position is in pixels, not an axis") {
    const auto result = ge::platform::parse_input_map(R"({"schema_version": 1, "bindings": [
        {"action": "pointer_x", "sources": {"mouse:x": 1}},
        {"action": "pointer_y", "sources": {"mouse:y": 1}}]})",
                                                      "input.json");
    REQUIRE(result.ok());
    RawState raw;
    raw.set("mouse:x", 412.0F);
    raw.set("mouse:y", 77.5F);
    LiveInput actions;
    result.map.apply(raw, actions);
    CHECK(actions.value("pointer_x") == 412.0F); // not clamped to 1
    CHECK(actions.value("pointer_y") == 77.5F);
}

TEST_CASE("input map: two sources of the same direction never go beyond 1") {
    const auto result = ge::platform::parse_input_map(arrows_file, "input.json");
    RawState raw;
    LiveInput actions;
    raw.set("key:right", 1.0F);
    raw.set("key:d", 1.0F);
    result.map.apply(raw, actions);
    CHECK(actions.value("move_x") == 1.0F);
}

TEST_CASE("input map: a second source of the same action fires it too") {
    const auto result = ge::platform::parse_input_map(arrows_file, "input.json");
    RawState raw;
    LiveInput actions;
    raw.set("mouse:left", 1.0F);
    result.map.apply(raw, actions);
    CHECK(actions.value("fire") == 1.0F);
}

TEST_CASE("input map: sources have the kind:name form") {
    CHECK(ge::platform::is_valid_source("key:space"));
    CHECK(ge::platform::is_valid_source("mouse:left"));
    CHECK(ge::platform::is_valid_source("pad:south"));
    CHECK(!ge::platform::is_valid_source("space"));
    CHECK(!ge::platform::is_valid_source("keyboard:space"));
    CHECK(!ge::platform::is_valid_source("key:"));
    CHECK(!ge::platform::is_valid_source("key:Space"));
}

TEST_CASE("input map: problems say where and why") {
    const auto bad_json = ge::platform::parse_input_map("{ nope", "input.json");
    CHECK(!bad_json.ok());
    CHECK(mentions(bad_json, "input.json: : invalid JSON"));

    CHECK(mentions(ge::platform::parse_input_map("[]", "i.json"), "must contain an object"));
    CHECK(
        mentions(ge::platform::parse_input_map(R"({"bindings": []})", "i.json"), "schema_version"));
    CHECK(mentions(ge::platform::parse_input_map(R"({"schema_version": 1})", "i.json"),
                   "bindings: must be an array"));
    CHECK(mentions(ge::platform::parse_input_map(
                       R"({"schema_version": 1, "bindings": [], "extra": 1})", "i.json"),
                   "unknown key \"extra\""));

    const auto wrong_source = ge::platform::parse_input_map(
        R"({"schema_version": 1, "bindings": [{"action": "jump", "sources": {"keyboard:x": 1}}]})",
        "i.json");
    CHECK(mentions(wrong_source, "bindings[0].sources.keyboard:x"));
    CHECK(mentions(wrong_source, "key, mouse, pad, touch"));

    CHECK(mentions(
        ge::platform::parse_input_map(
            R"({"schema_version": 1, "bindings": [{"action": "Jump", "sources": {"key:x": 1}}]})",
            "i.json"),
        "lower_snake_case"));
    CHECK(mentions(
        ge::platform::parse_input_map(
            R"({"schema_version": 1, "bindings": [{"action": "jump", "sources": {"key:x": "yes"}}]})",
            "i.json"),
        "scale must be a number"));
    CHECK(mentions(
        ge::platform::parse_input_map(
            R"({"schema_version": 1, "bindings": [{"action": "jump", "sources": {}}]})", "i.json"),
        "sources: must be an object"));
    CHECK(mentions(ge::platform::parse_input_map(
                       R"({"schema_version": 1, "bindings": [{"action": "jump"}]})", "i.json"),
                   "sources: must be an object"));
    CHECK(
        mentions(ge::platform::parse_input_map(
                     R"({"schema_version": 1, "bindings": [{"sources": {"key:x": 1}}]})", "i.json"),
                 "action: must be"));
    CHECK(mentions(
        ge::platform::parse_input_map(R"({"schema_version": 1, "bindings": [5]})", "i.json"),
        "must be an object {action, sources}"));
    CHECK(mentions(
        ge::platform::parse_input_map(
            R"({"schema_version": 1, "bindings": [{"action": "jump", "sources": {"key:x": 1}, "hint": 1}]})",
            "i.json"),
        "unknown key \"hint\""));
    CHECK(mentions(ge::platform::parse_input_map(
                       R"({"schema_version": 1, "bindings": [
                            {"action": "jump", "sources": {"key:x": 1}},
                            {"action": "jump", "sources": {"key:y": 1}}]})",
                       "i.json"),
                   "listed twice"));
}
