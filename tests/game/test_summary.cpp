// How a game presents itself (title, version, id, icon) and what `summarize` reports.
#include "ge/game/game.hpp"
#include "ge/game/runtime.hpp"
#include "ge/game/summary.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "../support/mini_test.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <string>

using ge::game::FileReader;
using ge::game::ModuleEntry;

namespace {

const std::array<ModuleEntry, 1> linked = {{
    {&ge::mod::transform2d::info, &ge::mod::transform2d::install},
}};

using Files = std::map<std::string, std::string>;

const char* const png_header = "\x89PNG\r\n\x1a\n";

Files sample() {
    return {
        {"g/game.json", R"({
            "schema_version": 1, "name": "star_hunt", "modules": ["transform2d"],
            "title": "Star Hunt", "version": "1.2.3", "id": "org.example.star_hunt",
            "icon": "art/icon.png", "window": {"width": 480, "height": 720},
            "scenes": ["main.json"], "start_scene": "main",
        })"},
        {"g/art/icon.png", std::string(png_header) + "pixels"},
        {"g/main.json", R"({"schema_version": 1, "name": "main", "entities": []})"},
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

ge::game::LoadedGame load_changed(const char* from, const char* to) {
    Files files = sample();
    std::string& text = files["g/game.json"];
    const std::size_t at = text.find(from);
    text = at == std::string::npos ? std::string("TEST SETUP: not found: ") + from
                                   : text.replace(at, std::string(from).size(), to);
    return load(files);
}

bool mentions(const ge::game::LoadedGame& loaded, const char* fragment) {
    return std::ranges::any_of(loaded.diagnostics, [fragment](const auto& diagnostic) {
        return diagnostic.to_string().find(fragment) != std::string::npos;
    });
}

} // namespace

TEST_CASE("summary: a game says how it presents itself") {
    const auto loaded = load(sample());
    for (const auto& diagnostic : loaded.diagnostics) {
        CHECK(diagnostic.to_string().empty());
    }
    REQUIRE(loaded.ok());
    const auto summary = ge::game::summarize(loaded, "games/star_hunt", "star_hunt");
    CHECK(summary.valid);
    CHECK(summary.problems.empty());
    CHECK(summary.directory == "games/star_hunt");
    CHECK(summary.name == "star_hunt");
    CHECK(summary.title == "Star Hunt");
    CHECK(summary.version == "1.2.3");
    CHECK(summary.id == "org.example.star_hunt");
    CHECK(summary.icon == "art/icon.png");
    CHECK(summary.window_width == 480U);
    CHECK(summary.window_height == 720U);
    CHECK(summary.modules.size() == 1U);
    CHECK(summary.scenes == 1U);
}

TEST_CASE("summary: what a game leaves out gets a default") {
    Files files = sample();
    files["g/game.json"] = R"({"schema_version": 1, "name": "star_hunt", "modules": ["transform2d"],
        "scenes": ["main.json"], "start_scene": "main"})";
    const auto loaded = load(files);
    REQUIRE(loaded.ok());
    const auto summary = ge::game::summarize(loaded, "g", "g");
    CHECK(summary.title == "Star hunt");
    CHECK(summary.version == "0.1.0");
    CHECK(summary.id == "ge.games.star_hunt");
    CHECK(summary.icon.empty());
    CHECK(summary.window_width == 800U);
    CHECK(ge::game::default_title("a") == "A");
    CHECK(ge::game::default_title("") == "");
    CHECK(ge::game::default_id("a") == "ge.games.a");
}

TEST_CASE("summary: a wrong version, id or icon is reported with the key") {
    CHECK(mentions(load_changed("\"1.2.3\"", "\"1.2\""), "version: \"1.2\" must be three numbers"));
    CHECK(mentions(load_changed("\"1.2.3\"", "\"1.2.x\""), "version"));
    CHECK(mentions(load_changed("\"1.2.3\"", "\"1..3\""), "version"));
    CHECK(mentions(load_changed("\"1.2.3\"", "\"1.2.3.4\""), "version"));
    CHECK(mentions(load_changed("\"org.example.star_hunt\"", "\"StarHunt\""), "id: \"StarHunt\""));
    CHECK(mentions(load_changed("\"org.example.star_hunt\"", "\"org..hunt\""), "id"));
    CHECK(mentions(load_changed("\"org.example.star_hunt\"", "\"org.hunt.\""), "id"));
    CHECK(mentions(load_changed("\"org.example.star_hunt\"", "\"1org.hunt\""), "id"));
    CHECK(mentions(load_changed("\"art/icon.png\"", "\"art/none.png\""), "cannot read the image"));
    CHECK(mentions(load_changed("\"art/icon.png\"", "\"../icon.png\""), "must be a relative path"));
    CHECK(mentions(load_changed("\"title\"", "\"titel\""), "unknown key"));

    Files files = sample();
    files["g/art/icon.png"] = "not a png";
    CHECK(mentions(load(files), "is not a PNG image"));
}

TEST_CASE("summary: an invalid game is still described, with its problems") {
    const auto loaded = load_changed("\"1.2.3\"", "\"nope\"");
    REQUIRE(!loaded.ok());
    const auto summary = ge::game::summarize(loaded, "games/star_hunt", "star_hunt");
    CHECK(!summary.valid);
    CHECK(summary.name == "star_hunt");
    CHECK(summary.title == "Star Hunt");
    CHECK(summary.version == "0.1.0"); // the wrong one is not kept
    REQUIRE(summary.problems.size() == 1U);
    CHECK(summary.problems[0].find("version") != std::string::npos);

    // A manifest that cannot be read at all falls back to the directory's name.
    const ge::game::LoadedGame empty;
    const auto fallback = ge::game::summarize(empty, "games/broken", "broken");
    CHECK(fallback.name == "broken");
    CHECK(fallback.title == "Broken");
    CHECK(fallback.id == "ge.games.broken");
}

TEST_CASE("summary: the JSON has what a launcher reads") {
    const auto loaded = load(sample());
    REQUIRE(loaded.ok());
    const ge::json::Value json =
        ge::game::to_json(ge::game::summarize(loaded, "games/star_hunt", "star_hunt"));
    REQUIRE(json.find("title") != nullptr);
    CHECK(json.find("title")->as_string() == "Star Hunt");
    CHECK(json.find("id")->as_string() == "org.example.star_hunt");
    CHECK(json.find("window")->find("height")->as_int() == 720);
    CHECK(json.find("valid")->as_bool());
    CHECK(json.find("problems")->items().empty());
    CHECK(json.find("modules")->items().size() == 1U);
}
