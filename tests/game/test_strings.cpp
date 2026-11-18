// The game's words, one file per language, and the fonts that show them.
#include "ge/app/app.hpp"
#include "ge/game/game.hpp"
#include "ge/game/runtime.hpp"
#include "ge/mod/text2d/text2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "../support/mini_test.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <string>

using ge::game::FileReader;
using ge::game::ModuleEntry;

namespace {

namespace text2d = ge::mod::text2d;

const std::array<ModuleEntry, 2> linked = {{
    {&ge::mod::transform2d::info, &ge::mod::transform2d::install},
    {&text2d::info, &text2d::install},
}};

using Files = std::map<std::string, std::string>;

Files sample() {
    return {
        {"g/game.json", R"({
            "schema_version": 1, "name": "words", "modules": ["transform2d", "text2d"],
            "strings": ["text/en.json", "text/fr.json"],
            "locale": "fr",
            "atlases": ["art/font.json"],
            "prefabs": [], "scenes": ["scenes/main.json"], "start_scene": "main",
        })"},
        {"g/text/en.json", R"({"schema_version": 1, "locale": "en", "entries": {
            "menu.start": "Play", "hud.score": "Score: {value}"}})"},
        {"g/text/fr.json", R"({"schema_version": 1, "locale": "fr", "entries": {
            "menu.start": "Jouer", "hud.score": "Points : {value}"}})"},
        {"g/art/font.json", R"({"schema_version": 1, "name": "font", "image": "font.png",
            "cell_width": 6, "cell_height": 9, "glyphs": "abé"})"},
        {"g/art/font.png", "PNG bytes"},
        {"g/scenes/main.json", R"({"schema_version": 1, "name": "main", "entities": [
            {"name": "title", "components": {
                "transform2d.Position": {"x": 10},
                "text2d.Text": {"string": "hud.score", "font": "font"}}},
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

ge::game::LoadedGame load_changed(const char* file, const char* from, const char* to) {
    Files files = sample();
    std::string& text = files[file];
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

TEST_CASE("strings: the keys of the first file make the game's string list") {
    const auto loaded = load(sample());
    for (const auto& diagnostic : loaded.diagnostics) {
        CHECK(diagnostic.to_string().empty());
    }
    REQUIRE(loaded.ok());
    REQUIRE(loaded.game.strings.size() == 2U);
    const std::vector<std::string> keys = loaded.game.ref_names("string");
    REQUIRE(keys.size() == 2U);
    CHECK(keys[0] == "menu.start");
    CHECK(keys[1] == "hud.score");
}

TEST_CASE("strings: the language in use gives the words, in the order of the keys") {
    const auto loaded = load(sample());
    REQUIRE(loaded.ok());
    const ge::app::TextTable table = ge::game::text_table_of(loaded.game);
    REQUIRE(table.texts.size() == 2U);
    CHECK(table.texts[0] == "Jouer"); // fr was picked
    CHECK(table.texts[1] == "Points : {value}");

    const auto english = load_changed("g/game.json", R"("locale": "fr")", R"("locale": "en")");
    REQUIRE(english.ok());
    CHECK(ge::game::text_table_of(english.game).texts[0] == "Play");

    const auto first = load_changed("g/game.json", R"("locale": "fr",)", "");
    REQUIRE(first.ok());
    CHECK(ge::game::text_table_of(first.game).texts[0] == "Play"); // no locale: the first file
}

TEST_CASE("strings: a font is an atlas with glyphs, decoded from UTF-8") {
    const auto loaded = load(sample());
    REQUIRE(loaded.ok());
    const ge::app::TextTable table = ge::game::text_table_of(loaded.game);
    REQUIRE(table.fonts.size() == 1U);
    CHECK(table.fonts[0].glyphs == U"abé");
    CHECK(table.fonts[0].cell_width == 6);
    CHECK(table.fonts[0].cell_height == 9);
}

TEST_CASE("strings: a text names its string, and the game gets it in the entity") {
    const auto loaded = load(sample());
    REQUIRE(loaded.ok());
    ge::app::App app;
    const ge::game::SetupResult setup = ge::game::setup_game(app, loaded.game, linked);
    REQUIRE(setup.ok());
    const auto* text = app.world().get<text2d::Text>(setup.find("title"));
    REQUIRE(text != nullptr);
    CHECK(text->string == 1U); // hud.score
    CHECK(text->font == 0U);
    REQUIRE(app.world().resource<ge::app::TextTable>() != nullptr);
}

TEST_CASE("strings: an unknown key says what exists") {
    const auto loaded = load_changed("g/scenes/main.json", "\"hud.score\"", "\"hud.scor\"");
    CHECK(!loaded.ok());
    CHECK(mentions(loaded, "no string is named \"hud.scor\""));
    CHECK(mentions(loaded, "did you mean \"hud.score\""));
}

TEST_CASE("strings: every language must have the same keys") {
    const auto missing = load_changed("g/text/fr.json", R"(, "hud.score": "Points : {value}")", "");
    CHECK(mentions(missing, "missing the key \"hud.score\" that en has"));
    const auto extra =
        load_changed("g/text/fr.json", R"("hud.score")", R"("hud.points": "x", "hud.score")");
    CHECK(mentions(extra, "this key is not in en"));
}

TEST_CASE("strings: the files and the locale are checked") {
    CHECK(mentions(load_changed("g/game.json", "\"locale\": \"fr\"", "\"locale\": \"de\""),
                   "no strings file is the language \"de\""));
    CHECK(mentions(load_changed("g/text/fr.json", R"("locale": "fr")", R"("locale": "en")"),
                   "another file already is the language"));
    CHECK(mentions(load_changed("g/text/en.json", "\"menu.start\"", "\"Menu Start\""),
                   "a key must be lowercase"));
    CHECK(mentions(load_changed("g/text/en.json", "\"Play\"", "3"), "expected text"));
    CHECK(mentions(load_changed("g/text/en.json", "\"entries\"", "\"entrees\""),
                   "missing required key \"entries\""));
    CHECK(mentions(load_changed("g/text/en.json", "\"locale\": \"en\"", "\"locale\": \"EN\""),
                   "lower_snake_case"));
}

TEST_CASE("strings: the lists the manifest builds cannot also be declared by hand") {
    CHECK(mentions(load_changed("g/game.json", R"("locale": "fr",)",
                                "\"locale\": \"fr\", \"lists\": {\"string\": [\"x\"]},"),
                   "the string list is made by"));
}
