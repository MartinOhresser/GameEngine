// Atlases and fields that name an item of the game ("ref" fields).
#include "ge/app/render.hpp"
#include "ge/game/game.hpp"
#include "ge/game/runtime.hpp"
#include "ge/mod/sprite2d/sprite2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "../support/mini_test.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <string>

using ge::game::FileReader;
using ge::game::ModuleEntry;

namespace {

namespace sprite2d = ge::mod::sprite2d;

const std::array<ModuleEntry, 2> linked = {{
    {&ge::mod::transform2d::info, &ge::mod::transform2d::install},
    {&ge::mod::sprite2d::info, &ge::mod::sprite2d::install},
}};

using Files = std::map<std::string, std::string>;

ge::game::Catalog catalog() {
    return ge::game::catalog_of(linked);
}

/// A small game with two atlases and a prefab that names one of them.
Files sample() {
    return {
        {"g/game.json", R"({
            "schema_version": 1, "name": "pictures",
            "modules": ["transform2d", "sprite2d"],
            "atlases": ["art/tiles.json", "art/hero.json"],
            "prefabs": ["prefabs/tile.json"],
            "scenes": ["scenes/main.json"],
            "start_scene": "main",
        })"},
        {"g/art/tiles.json", R"({"schema_version": 1, "name": "tiles", "image": "tiles.png",
                                  "cell_width": 16, "cell_height": 16})"},
        {"g/art/tiles.png", "PNG bytes"},
        {"g/art/hero.json", R"({"schema_version": 1, "name": "hero", "image": "hero.png",
                                 "cell_width": 24, "cell_height": 32})"},
        {"g/art/hero.png", "PNG bytes"},
        {"g/prefabs/tile.json", R"({"schema_version": 1, "name": "tile",
            "components": {"transform2d.Position": {}, "sprite2d.Sprite": {"atlas": "hero", "frame": 2}}})"},
        {"g/scenes/main.json", R"({"schema_version": 1, "name": "main", "entities": [
            {"name": "a", "prefab": "tile"},
            {"name": "b", "prefab": "tile", "components": {"sprite2d.Sprite": {"atlas": "tiles"}}},
            {"name": "c", "prefab": "tile", "components": {"sprite2d.Sprite": {"atlas": 1}}},
        ]})"},
    };
}

FileReader reader_of(const Files& files) {
    return [&files](const std::string& path) -> std::optional<std::string> {
        const auto found = files.find(path);
        if (found == files.end()) {
            return std::nullopt;
        }
        return found->second;
    };
}

ge::game::LoadedGame load(const Files& files) {
    return ge::game::load_game("g/game.json", reader_of(files), catalog());
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
    if (at != std::string::npos) {
        text.replace(at, std::string(from).size(), to);
    } else {
        text = std::string("TEST SETUP: text not found: ") + from;
    }
    return load(files);
}

} // namespace

TEST_CASE("atlas: the atlases of a game are read in manifest order") {
    const auto loaded = load(sample());
    for (const auto& diagnostic : loaded.diagnostics) {
        CHECK(diagnostic.to_string().empty());
    }
    REQUIRE(loaded.ok());
    REQUIRE(loaded.game.atlases.size() == 2U);
    CHECK(loaded.game.atlases[0].name == "tiles");
    CHECK(loaded.game.atlases[0].image == "g/art/tiles.png");
    CHECK(loaded.game.atlases[0].cell_width == 16);
    CHECK(loaded.game.atlases[1].name == "hero");
    CHECK(loaded.game.atlases[1].cell_height == 32);
    const std::vector<std::string> names = loaded.game.ref_names("atlas");
    REQUIRE(names.size() == 2U);
    CHECK(names[1] == "hero");
    CHECK(loaded.game.ref_names("sound").empty());
}

TEST_CASE("atlas: an item named in a ref field becomes its index in the entity") {
    const auto loaded = load(sample());
    REQUIRE(loaded.ok());
    ge::app::App app;
    const ge::game::SetupResult setup = ge::game::setup_game(app, loaded.game, linked);
    REQUIRE(setup.ok());
    const auto sprite_of = [&](const char* name) {
        const auto* sprite = app.world().get<sprite2d::Sprite>(setup.find(name));
        CHECK(sprite != nullptr);
        return sprite != nullptr ? *sprite : sprite2d::Sprite{};
    };
    CHECK(sprite_of("a").atlas == 1U); // "hero", from the prefab
    CHECK(sprite_of("a").frame == 2U);
    CHECK(sprite_of("b").atlas == 0U); // "tiles", the entity's own value
    CHECK(sprite_of("b").frame == 2U); // the prefab's frame is kept
    CHECK(sprite_of("c").atlas == 1U); // a plain number is the index itself
}

TEST_CASE("atlas: an unknown name or a number out of range says what exists") {
    const auto unknown = load_changed("g/prefabs/tile.json", "\"hero\"", "\"heroe\"");
    CHECK(!unknown.ok());
    CHECK(mentions(unknown, "no atlas is named \"heroe\""));
    CHECK(mentions(unknown, "did you mean \"hero\""));
    CHECK(mentions(unknown, "tiles, hero"));

    const auto range = load_changed("g/prefabs/tile.json", "\"hero\"", "7");
    CHECK(!range.ok());
    CHECK(mentions(range, "there is no atlas number 7"));
}

TEST_CASE("atlas: a name needs the game to declare atlases at all") {
    Files files = sample();
    files["g/game.json"] = R"({
        "schema_version": 1, "name": "pictures", "modules": ["transform2d", "sprite2d"],
        "prefabs": ["prefabs/tile.json"], "scenes": ["scenes/main.json"], "start_scene": "main"})";
    const auto loaded = load(files);
    CHECK(!loaded.ok());
    CHECK(mentions(loaded, "the game declares none"));
}

TEST_CASE("atlas: a broken atlas file is reported where it is") {
    CHECK(mentions(load_changed("g/art/tiles.json", "tiles.png", "missing.png"),
                   "cannot read the image"));
    CHECK(mentions(load_changed("g/art/tiles.json", "tiles.png", "../x.png"), "relative path"));
    CHECK(mentions(load_changed("g/art/tiles.json", "\"cell_width\": 16", "\"cell_width\": 0"),
                   "cell_width"));
    CHECK(mentions(load_changed("g/art/tiles.json", "\"name\": \"tiles\"", "\"name\": \"hero\""),
                   "two atlases are named \"hero\""));
    CHECK(mentions(load_changed("g/art/tiles.json", "\"image\"", "\"imagen\""), "image"));
}

TEST_CASE("atlas: load_atlases hands every picture to the renderer, and reports failures") {
    const auto loaded = load(sample());
    REQUIRE(loaded.ok());
    ge::app::RecordingRenderer renderer;
    CHECK(ge::game::load_atlases(loaded.game, renderer).empty());
    REQUIRE(renderer.atlases().size() == 2U);
    CHECK(renderer.atlases()[1].image == "g/art/hero.png");
    CHECK(renderer.atlases()[1].cell_width == 24);
}

namespace {
/// The sample game, its hero atlas given this "filter".
ge::game::LoadedGame with_filter(const char* filter) {
    const std::string value = std::string(R"("filter": ")") + filter + R"(", "cell_width": 24)";
    return load_changed("g/art/hero.json", R"("cell_width": 24)", value.c_str());
}
} // namespace

TEST_CASE("atlas: \"filter\": \"linear\" smooths an atlas, pixel art stays sharp by default") {
    const auto smooth = with_filter("linear");
    REQUIRE(smooth.ok());
    CHECK(!smooth.game.atlases[0].smooth);
    CHECK(smooth.game.atlases[1].smooth);
    ge::app::RecordingRenderer renderer;
    CHECK(ge::game::load_atlases(smooth.game, renderer).empty());
    REQUIRE(renderer.atlases().size() == 2U);
    CHECK(!renderer.atlases()[0].smooth);
    CHECK(renderer.atlases()[1].smooth);
}

TEST_CASE("atlas: \"filter\" is nearest or linear, nothing else") {
    const auto sharp = with_filter("nearest");
    REQUIRE(sharp.ok());
    CHECK(!sharp.game.atlases[1].smooth);

    const auto wrong = with_filter("blurry");
    CHECK(!wrong.ok());
    CHECK(mentions(wrong, "\"linear\""));
}
