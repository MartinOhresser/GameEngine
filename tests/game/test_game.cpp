#include "ge/app/time.hpp"
#include "ge/core/random.hpp"
#include "ge/game/game.hpp"
#include "ge/game/runtime.hpp"
#include "ge/mod/lifetime/lifetime.gen.hpp"
#include "ge/mod/screen_wrap/screen_wrap.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "../support/mini_test.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <string>

using ge::game::Catalog;
using ge::game::FileReader;
using ge::game::ModuleEntry;

namespace {

const std::array<ModuleEntry, 3> linked = {{
    {&ge::mod::transform2d::info, &ge::mod::transform2d::install},
    {&ge::mod::lifetime::info, &ge::mod::lifetime::install},
    {&ge::mod::screen_wrap::info, &ge::mod::screen_wrap::install},
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

/// A small valid game; tests change one file or piece at a time.
Files sample_game() {
    return {
        {"g/game.json", R"({
            // A tiny game.
            "schema_version": 1,
            "name": "demo",
            "description": "Test game",
            "modules": ["screen_wrap", "transform2d", "lifetime"],
            "simulation": {"tick_rate": 30, "seed": 99},
            "params": {"transform2d": {"speed_scale": 2}},
            "prefabs": ["prefabs/mover.json", "prefabs/fast_mover.json"],
            "scenes": ["scenes/main.json"],
            "start_scene": "main",
        })"},
        {"g/prefabs/mover.json", R"({
            "schema_version": 1, "name": "mover",
            "components": {
                "transform2d.Position": {"x": 10, "y": 20},
                "transform2d.Velocity": {"x": 1.5},
                "screen_wrap.WrapArea": {},
            }
        })"},
        {"g/prefabs/fast_mover.json", R"({
            "schema_version": 1, "name": "fast_mover", "extends": "mover",
            "components": {"transform2d.Velocity": {"y": 7}}
        })"},
        {"g/scenes/main.json", R"({
            "schema_version": 1, "name": "main",
            "entities": [
                {"name": "hero", "prefab": "fast_mover",
                 "components": {"transform2d.Position": {"x": 55}}},
                {"name": "spark", "components": {"lifetime.Lifetime": {"ticks": 5}}},
                {"prefab": "mover"},
            ]
        })"},
    };
}

Catalog linked_catalog() {
    return ge::game::catalog_of(linked);
}

ge::game::LoadedGame load(const Files& files) {
    return ge::game::load_game("g/game.json", reader_of(files), linked_catalog());
}

bool mentions(const ge::game::LoadedGame& loaded, const char* fragment) {
    return std::ranges::any_of(loaded.diagnostics, [fragment](const auto& diagnostic) {
        return diagnostic.to_string().find(fragment) != std::string::npos;
    });
}

/// The sample game with `from` replaced by `to` in one file.
ge::game::LoadedGame load_changed(const char* file, const char* from, const char* to) {
    Files files = sample_game();
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

TEST_CASE("game: a valid game loads completely") {
    const auto loaded = load(sample_game());
    for (const auto& diagnostic : loaded.diagnostics) {
        CHECK(diagnostic.to_string().empty()); // prints the problem if there is one
    }
    REQUIRE(loaded.ok());
    const auto& game = loaded.game;
    CHECK(game.manifest.name == "demo");
    CHECK(game.manifest.tick_rate == 30U);
    CHECK(game.manifest.seed == 99U);
    CHECK(game.manifest.modules.size() == 3U);
    REQUIRE(game.manifest.params.size() == 1U);
    CHECK(game.manifest.params[0].name == "speed_scale");
    CHECK(game.prefabs.size() == 2U);
    REQUIRE(game.scenes.size() == 1U);
    CHECK(game.scenes[0].entities.size() == 3U);
    CHECK(game.find_prefab("mover") != nullptr);
    CHECK(game.find_prefab("nope") == nullptr);
    CHECK(game.find_scene("main") != nullptr);
    CHECK(game.find_scene("nope") == nullptr);
}

TEST_CASE("game: a manifest path written with backslashes (Windows) finds its files") {
    Files files;
    for (const auto& [path, text] : sample_game()) {
        files["g\\" + path.substr(2)] = text;
    }
    const auto loaded = ge::game::load_game("g\\game.json", reader_of(files), linked_catalog());
    CHECK(loaded.ok());
    CHECK(loaded.game.scenes.size() == 1U);
}

TEST_CASE("game: prefab chains merge field by field, then the entity's own values") {
    const auto loaded = load(sample_game());
    REQUIRE(loaded.ok());
    const auto& entity = loaded.game.scenes[0].entities[0];
    const auto components =
        ge::game::resolve_components(loaded.game, entity.prefab, entity.components);
    REQUIRE(components.size() == 3U);
    CHECK(components[0].name == "transform2d.Position");
    CHECK(components[0].fields.find("x")->as_int() == 55); // the entity's override
    CHECK(components[0].fields.find("y")->as_int() == 20); // from the base prefab
    CHECK(components[1].name == "transform2d.Velocity");
    CHECK(components[1].fields.find("x")->as_real() == 1.5); // from the base prefab
    CHECK(components[1].fields.find("y")->as_int() == 7);    // from the child prefab
    CHECK(components[2].name == "screen_wrap.WrapArea");
    // No prefab: just the entity's own components.
    const auto own =
        ge::game::resolve_components(loaded.game, "", loaded.game.scenes[0].entities[1].components);
    CHECK(own.size() == 1U);
}

TEST_CASE("game: files that cannot be read or parsed") {
    Files files = sample_game();
    files.erase("g/game.json");
    CHECK(mentions(load(files), "g/game.json: cannot read the file"));

    files = sample_game();
    files["g/game.json"] = "{ not json";
    CHECK(mentions(load(files), "g/game.json: invalid JSON at"));

    files = sample_game();
    files.erase("g/scenes/main.json");
    CHECK(mentions(load(files),
                   "g/game.json: scenes[0]: cannot read the file \"g/scenes/main.json\""));

    files = sample_game();
    files["g/prefabs/mover.json"] = "[1,";
    CHECK(mentions(load(files), "g/prefabs/mover.json: invalid JSON"));

    files = sample_game();
    files["g/game.json"] = "[]";
    CHECK(mentions(load(files), "expected an object"));
}

TEST_CASE("game: manifest problems") {
    CHECK(mentions(load_changed("g/game.json", "\"schema_version\": 1", "\"schema_version\": 2"),
                   "only schema version 1"));
    CHECK(mentions(load_changed("g/game.json", "\"schema_version\": 1,", ""),
                   "missing required key \"schema_version\""));
    CHECK(mentions(load_changed("g/game.json", "\"name\": \"demo\"", "\"name\": \"Demo\""),
                   "lower_snake_case"));
    CHECK(mentions(
        load_changed("g/game.json", "\"description\": \"Test game\"", "\"description\": 1"),
        "expected a string"));
    CHECK(mentions(
        load_changed("g/game.json", "\"start_scene\": \"main\"", "\"start_scene\": \"mian\""),
        "no scene is named \"mian\" (did you mean \"main\"?)"));
    CHECK(mentions(load_changed("g/game.json", "\"modules\"", "\"colour\": 1, \"modules\""),
                   "unknown key"));
    CHECK(
        mentions(load_changed("g/game.json",
                              "\"modules\": [\"screen_wrap\", \"transform2d\", \"lifetime\"],", ""),
                 "missing required key \"modules\""));
    CHECK(mentions(load_changed("g/game.json", "\"lifetime\"]", "\"lifetim\"]"),
                   "unknown module \"lifetim\" (did you mean \"lifetime\"?)"));
    CHECK(mentions(load_changed("g/game.json", "\"lifetime\"]", "\"lifetime\", \"lifetime\"]"),
                   "listed twice"));
    CHECK(mentions(
        load_changed("g/game.json", "\"screen_wrap\", \"transform2d\", ", "\"screen_wrap\", "),
        "needs \"transform2d\", which is not listed"));
    CHECK(mentions(load_changed("g/game.json", "\"prefabs/mover.json\"", "\"../mover.json\""),
                   "must be a relative path inside the game's directory"));
    CHECK(mentions(load_changed("g/game.json", "\"prefabs/mover.json\"", "\"/etc/passwd\""),
                   "relative path"));
}

TEST_CASE("game: the window size is a setting of the game, 800 by 600 by default") {
    const auto plain = load(sample_game());
    REQUIRE(plain.ok());
    CHECK(plain.game.manifest.window_width == 800U);
    CHECK(plain.game.manifest.window_height == 600U);
    const auto sized = load_changed("g/game.json", R"("seed": 99})", R"("seed": 99},
        "window": {"width": 320, "height": 180})");
    REQUIRE(sized.ok());
    CHECK(sized.game.manifest.window_width == 320U);
    CHECK(sized.game.manifest.window_height == 180U);
    CHECK(mentions(
        load_changed("g/game.json", R"("seed": 99})", R"("seed": 99}, "window": {"width": 0})"),
        "1 to 16384"));
    CHECK(mentions(
        load_changed("g/game.json", R"("seed": 99})", R"("seed": 99}, "window": {"w": 3})"),
        "unknown key"));
}

TEST_CASE("game: a window can follow the shape of the screen, `adapt`") {
    const auto with = [](const std::string& window) {
        const std::string changed = R"("seed": 99}, "window": )" + window;
        return load_changed("g/game.json", R"("seed": 99})", changed.c_str());
    };
    CHECK(!load(sample_game()).game.manifest.window_adapt);
    // true: the usual shapes of phones for a tall game, of wide screens for a wide one.
    const auto tall = with(R"({"width": 480, "height": 720, "adapt": true})");
    REQUIRE(tall.ok());
    CHECK(tall.game.manifest.window_adapt);
    CHECK(tall.game.manifest.window_min_aspect == 0.4F);
    CHECK(tall.game.manifest.window_max_aspect == 0.8F);
    const auto wide = with(R"({"width": 800, "height": 600, "adapt": true})");
    REQUIRE(wide.ok());
    CHECK(wide.game.manifest.window_min_aspect == 1.25F);
    CHECK(wide.game.manifest.window_max_aspect == 2.4F);
    // Or the limits, which include the window's own shape.
    const auto custom =
        with(R"({"width": 480, "height": 720, "adapt": {"min_aspect": 0.5, "max_aspect": 0.7}})");
    REQUIRE(custom.ok());
    CHECK(custom.game.manifest.window_min_aspect == 0.5F);
    CHECK(custom.game.manifest.window_max_aspect == 0.7F);
    CHECK(!with(R"({"adapt": false})").game.manifest.window_adapt);
    CHECK(mentions(with(R"({"width": 480, "height": 720, "adapt": {"min_aspect": 0.7}})"),
                   "include the window's own"));
    CHECK(mentions(with(R"({"adapt": {"min_aspect": -1}})"), "the width divided by the height"));
    CHECK(mentions(with(R"({"adapt": {"mid": 1}})"), "unknown key"));
    CHECK(mentions(with(R"({"adapt": 3})"), "expected true, false or"));
}

TEST_CASE("game: simulation settings are checked") {
    CHECK(mentions(load_changed("g/game.json", "\"tick_rate\": 30", "\"tick_rate\": 0"),
                   "1 to 1000"));
    CHECK(mentions(load_changed("g/game.json", "\"tick_rate\": 30", "\"tick_rate\": 2000"),
                   "1 to 1000"));
    CHECK(mentions(load_changed("g/game.json", "\"tick_rate\": 30", "\"tick_rate\": 30.5"),
                   "1 to 1000"));
    CHECK(mentions(load_changed("g/game.json", "\"seed\": 99", "\"seed\": -1"), "zero or more"));
    CHECK(mentions(load_changed("g/game.json", "\"seed\": 99", "\"seed\": \"x\""), "zero or more"));
    CHECK(mentions(load_changed("g/game.json", "\"seed\": 99", "\"seeds\": 99"),
                   "unknown key (did you mean \"seed\"?)"));
    CHECK(mentions(load_changed("g/game.json", "{\"tick_rate\": 30, \"seed\": 99}", "5"),
                   "expected an object"));
}

TEST_CASE("game: params are checked against the module's declarations") {
    CHECK(mentions(load_changed("g/game.json", "\"speed_scale\": 2", "\"speed_scal\": 2"),
                   "has no param named \"speed_scal\" (did you mean \"speed_scale\"?)"));
    CHECK(mentions(load_changed("g/game.json", "\"speed_scale\": 2", "\"speed_scale\": 20"),
                   "outside the bounds"));
    CHECK(mentions(load_changed("g/game.json", "\"speed_scale\": 2", "\"speed_scale\": -1"),
                   "outside the bounds"));
    CHECK(mentions(load_changed("g/game.json", "\"speed_scale\": 2", "\"speed_scale\": \"fast\""),
                   "expected a value of type f32, found a string"));
    CHECK(mentions(load_changed("g/game.json", "\"transform2d\": {\"speed_scale\": 2}",
                                "\"lifetime\": {\"x\": 1}"),
                   "module \"lifetime\" has no param named \"x\""));
    CHECK(mentions(
        load_changed("g/game.json", "\"transform2d\": {\"speed_scale\": 2}", "\"ghost\": {}"),
        "not one of the manifest's modules"));
    CHECK(mentions(
        load_changed("g/game.json", "\"transform2d\": {\"speed_scale\": 2}", "\"transform2d\": 3"),
        "expected an object"));
    CHECK(mentions(load_changed("g/game.json", "{\"transform2d\": {\"speed_scale\": 2}}", "[]"),
                   "expected an object"));
}

TEST_CASE("game: component problems say where, and suggest a fix") {
    CHECK(mentions(
        load_changed("g/prefabs/mover.json", "\"transform2d.Position\"", "\"transform2d.Positon\""),
        "unknown component (did you mean \"transform2d.Position\"?)"));
    CHECK(mentions(load_changed("g/prefabs/mover.json", "\"transform2d.Position\"", "\"Position\""),
                   "a component is written \"module.Component\""));
    CHECK(mentions(
        load_changed("g/prefabs/mover.json", "\"transform2d.Position\"", "\"physics.Body\""),
        "module \"physics\" is not listed in the manifest's modules"));
    CHECK(mentions(
        load_changed("g/prefabs/mover.json", "{\"x\": 10, \"y\": 20}", "{\"x\": 10, \"z\": 20}"),
        "components.transform2d.Position.z: unknown field"));
    CHECK(mentions(
        load_changed("g/prefabs/mover.json", "{\"x\": 10, \"y\": 20}", "{\"x\": 10, \"yy\": 20}"),
        "(did you mean \"y\"?)"));
    CHECK(mentions(load_changed("g/prefabs/mover.json", "\"screen_wrap.WrapArea\": {}",
                                "\"screen_wrap.WrapArea\": {\"w\": 1}"),
                   "expected one of: width, height"));
    CHECK(mentions(
        load_changed("g/prefabs/mover.json", "{\"x\": 10, \"y\": 20}", "{\"x\": \"ten\"}"),
        "components.transform2d.Position.x: expected a value of type f32, found a string"));
    CHECK(mentions(load_changed("g/scenes/main.json", "{\"ticks\": 5}", "{\"ticks\": 5.5}"),
                   "entities[1].components.lifetime.Lifetime.ticks: the value does not fit"));
    CHECK(mentions(load_changed("g/scenes/main.json", "{\"ticks\": 5}", "{\"ticks\": 3000000000}"),
                   "does not fit the type i32"));
    CHECK(mentions(load_changed("g/prefabs/mover.json", "{\"x\": 10, \"y\": 20}", "[1]"),
                   "expected an object"));
    CHECK(mentions(
        load_changed("g/prefabs/mover.json", "\"components\": {", "\"components\": [], \"x\": {"),
        "expected an object"));
    CHECK(mentions(load_changed("g/prefabs/mover.json", "\"transform2d.Velocity\": {\"x\": 1.5}",
                                "\"lifetime.Lifetime\": {\"x\": 1}"),
                   "expected one of: ticks"));
}

TEST_CASE("game: prefab problems") {
    CHECK(mentions(load_changed("g/prefabs/fast_mover.json", "\"extends\": \"mover\"",
                                "\"extends\": \"movr\""),
                   "unknown prefab \"movr\" (did you mean \"mover\"?)"));
    CHECK(mentions(
        load_changed("g/prefabs/mover.json", "\"name\": \"mover\"", "\"name\": \"fast_mover\""),
        "another prefab is already named \"fast_mover\""));
    CHECK(mentions(load_changed("g/prefabs/mover.json", "\"name\": \"mover\",",
                                "\"name\": \"mover\", \"extends\": \"fast_mover\","),
                   "loops"));
    CHECK(mentions(load_changed("g/prefabs/mover.json", "\"name\": \"mover\",",
                                "\"name\": \"mover\", \"extends\": \"mover\","),
                   "loops"));
    CHECK(mentions(
        load_changed("g/prefabs/mover.json", "\"schema_version\": 1", "\"schema_version\": 3"),
        "only schema version 1"));
    CHECK(mentions(load_changed("g/prefabs/mover.json", "\"schema_version\": 1,", ""),
                   "missing required key \"schema_version\""));
    CHECK(mentions(load_changed("g/prefabs/mover.json", "\"schema_version\": 1,",
                                "\"schema_version\": 1, \"colour\": 1,"),
                   "unknown key"));
    CHECK(mentions(load_changed("g/prefabs/mover.json", "\"name\": \"mover\"", "\"name\": 4"),
                   "expected a string"));
}

TEST_CASE("game: scene problems") {
    CHECK(mentions(
        load_changed("g/scenes/main.json", "\"prefab\": \"fast_mover\"",
                     "\"prefab\": \"fast_move\""),
        "entities[0].prefab: unknown prefab \"fast_move\" (did you mean \"fast_mover\"?)"));
    CHECK(mentions(load_changed("g/scenes/main.json", "\"name\": \"spark\"", "\"name\": \"hero\""),
                   "two entities of the scene are named \"hero\""));
    CHECK(mentions(load_changed("g/scenes/main.json", "{\"prefab\": \"mover\"}", "{}"),
                   "an entity needs a \"prefab\", \"components\", or both"));
    CHECK(mentions(load_changed("g/scenes/main.json", "{\"prefab\": \"mover\"}", "3"),
                   "expected an object"));
    CHECK(mentions(load_changed("g/scenes/main.json", "{\"prefab\": \"mover\"}",
                                "{\"prefab\": \"mover\", \"tag\": 1}"),
                   "unknown key"));
    CHECK(
        mentions(load_changed("g/scenes/main.json", "\"entities\": [", "\"entities\": 5, \"x\": ["),
                 "expected an array"));
    CHECK(mentions(load_changed("g/scenes/main.json", "\"entities\": [", "\"others\": ["),
                   "missing required key \"entities\""));
    CHECK(mentions(load_changed("g/scenes/main.json", "\"name\": \"main\"", "\"name\": \"Main\""),
                   "lower_snake_case"));
    CHECK(mentions(
        load_changed("g/scenes/main.json", "\"schema_version\": 1", "\"schema_version\": 0"),
        "only schema version 1"));
    CHECK(mentions(load_changed("g/scenes/main.json", "\"schema_version\": 1,", ""),
                   "missing required key \"schema_version\""));
    CHECK(mentions(load_changed("g/scenes/main.json", "\"schema_version\": 1,",
                                "\"schema_version\": 1, \"colour\": 1,"),
                   "unknown key"));
    CHECK(mentions(load_changed("g/game.json", "\"scenes\": [\"scenes/main.json\"]",
                                "\"scenes\": [\"scenes/main.json\", \"scenes/other.json\"]"),
                   "cannot read the file \"g/scenes/other.json\""));
    Files files = sample_game();
    files["g/scenes/other.json"] = files["g/scenes/main.json"];
    files["g/game.json"] = sample_game()["g/game.json"];
    const std::string needle = "\"scenes/main.json\"";
    files["g/game.json"].replace(files["g/game.json"].find(needle), needle.size(),
                                 R"("scenes/main.json", "scenes/other.json")");
    CHECK(mentions(load(files), "another scene is already named \"main\""));
}

TEST_CASE("game: the catalog can also come from module.json files") {
    std::vector<ge::module::ModuleSpec> specs;
    for (const char* text : {
             R"({"schema_version": 1, "name": "a", "version": "1.0.0",
                 "params": [{"name": "p", "type": "i32", "default": 1, "min": 0, "max": 5}],
                 "components": [{"name": "C", "replicable": false, "fields": [{"name": "f", "type": "bool"}]}]})",
             R"({"schema_version": 1, "name": "b", "version": "1.0.0", "depends": ["a"]})"}) {
        auto result = ge::module::load_module(text, "x");
        REQUIRE(result.ok());
        specs.push_back(std::move(result.spec));
    }
    const Catalog catalog = Catalog::from_specs(specs);
    REQUIRE(catalog.find_module("a") != nullptr);
    CHECK(catalog.find_module("b")->depends == std::vector<std::string>{"a"});
    CHECK(catalog.find_module("a")->find_param("p")->max.as_int() == 5);
    CHECK(catalog.find_component("a.C")->find_field("f")->type == ge::module::FieldType::Bool);
    CHECK(catalog.find_component("a.Nope") == nullptr);
    CHECK(catalog.find_component("nope.C") == nullptr);
    CHECK(catalog.find_component("noDot") == nullptr);
    CHECK(catalog.find_module("zzz") == nullptr);
    CHECK(catalog.find_module("a")->find_param("zzz") == nullptr);
    CHECK(catalog.find_module("a")->find_component("zzz") == nullptr);
    CHECK(catalog.find_component("a.C")->find_field("zzz") == nullptr);
}

TEST_CASE("game: the catalog from linked modules knows bounds and fields") {
    const Catalog catalog = linked_catalog();
    const auto* scale = catalog.find_module("transform2d")->find_param("speed_scale");
    REQUIRE(scale != nullptr);
    CHECK(scale->min.as_real() == 0.0);
    CHECK(scale->max.as_real() == 10.0);
    CHECK(catalog.find_component("transform2d.Position")->fields.size() == 2U);
    CHECK(catalog.find_module("screen_wrap")->depends == std::vector<std::string>{"transform2d"});
    // A param without bounds has none in the catalog either.
    CHECK(catalog.find_component("lifetime.Lifetime")->find_field("ticks")->type ==
          ge::module::FieldType::I32);
}

TEST_CASE("setup: a game starts from its manifest and scene") {
    const auto loaded = load(sample_game());
    REQUIRE(loaded.ok());
    ge::app::App app;
    const auto result = ge::game::setup_game(app, loaded.game, linked);
    REQUIRE(result.ok());
    CHECK(app.module_names() ==
          (std::vector<std::string>{"transform2d", "screen_wrap", "lifetime"}));
    CHECK(app.world().resource<ge::app::Time>()->tick_rate == 30U);
    CHECK(app.world().resource<ge::Random>()->state() == ge::Random{99}.state());
    CHECK(app.world().resource<ge::mod::transform2d::Params>()->speed_scale == 2.0F);
    CHECK(app.world().entity_count() == 3U);

    const ge::ecs::Entity hero = result.find("hero");
    REQUIRE(app.world().is_alive(hero));
    const auto* position = app.world().get<ge::mod::transform2d::Position>(hero);
    REQUIRE(position != nullptr);
    CHECK(position->x == 55.0F);
    CHECK(position->y == 20.0F);
    const auto* velocity = app.world().get<ge::mod::transform2d::Velocity>(hero);
    REQUIRE(velocity != nullptr);
    CHECK(velocity->x == 1.5F);
    CHECK(velocity->y == 7.0F);
    CHECK(app.world().get<ge::mod::screen_wrap::WrapArea>(hero)->width == 800.0F);
    CHECK(app.world().get<ge::mod::lifetime::Lifetime>(result.find("spark"))->ticks == 5);
    CHECK(result.find("nobody").is_null());
}

TEST_CASE("setup: the game runs, and the same file always gives the same game") {
    const auto loaded = load(sample_game());
    REQUIRE(loaded.ok());
    const auto run = [&loaded] {
        ge::app::App app;
        const auto result = ge::game::setup_game(app, loaded.game, linked);
        CHECK(result.ok());
        app.run_frames(10);
        return std::pair{app.world().fingerprint(), app.world().entity_count()};
    };
    const auto first = run();
    const auto second = run();
    CHECK(first == second);
    CHECK(first.second == 2U); // the spark lived 5 ticks, so only the two movers remain
}

TEST_CASE("setup: problems are reported, not ignored") {
    const auto loaded = load(sample_game());
    REQUIRE(loaded.ok());
    const std::array<ModuleEntry, 1> only_transform = {linked[0]};
    ge::app::App missing;
    const auto result = ge::game::setup_game(missing, loaded.game, only_transform);
    CHECK(!result.ok());
    CHECK(result.problems[0].find("not linked into this program") != std::string::npos);

    ge::app::App app;
    const auto scene = ge::game::spawn_scene(app, loaded.game, "main");
    CHECK(!scene.ok()); // no module is installed: the components are not available
    CHECK(scene.problems[0].find("its module is not installed") != std::string::npos);
    CHECK(!ge::game::spawn_scene(app, loaded.game, "ghost").ok());
}

TEST_CASE("setup: a module whose dependency is not part of the game is reported") {
    const auto loaded = load(sample_game());
    REQUIRE(loaded.ok());
    // screen_wrap alone: its dependency is not part of the game.
    auto game = loaded.game;
    game.manifest.modules = {"screen_wrap"};
    ge::app::App app;
    const auto result = ge::game::setup_game(app, game, linked);
    CHECK(!result.ok());
}

TEST_CASE("game: the input file binds keys to actions") {
    Files files = sample_game();
    files["g/game.json"].replace(files["g/game.json"].find("\"start_scene\""), 0,
                                 "\"input\": \"keys.json\",\n            ");
    files["g/keys.json"] = R"({
        "schema_version": 1,
        "bindings": [{ "action": "move_x", "sources": { "key:right": 1, "key:left": -1 } }]
    })";
    const auto loaded = load(files);
    for (const auto& diagnostic : loaded.diagnostics) {
        CHECK(diagnostic.to_string().empty());
    }
    REQUIRE(loaded.ok());
    CHECK(loaded.game.manifest.input_file == "keys.json");
    CHECK(loaded.game.input.bindings().size() == 2U);
}

TEST_CASE("game: a game without an input file has no bindings") {
    const auto loaded = load(sample_game());
    REQUIRE(loaded.ok());
    CHECK(loaded.game.manifest.input_file.empty());
    CHECK(loaded.game.input.bindings().empty());
}

TEST_CASE("game: input file problems are reported with the file and the place") {
    Files files = sample_game();
    files["g/game.json"].replace(files["g/game.json"].find("\"start_scene\""), 0,
                                 "\"input\": \"keys.json\",\n            ");
    // The file is missing.
    CHECK(mentions(load(files), "game.json: input: cannot read the file \"g/keys.json\""));
    // The file has a wrong source.
    files["g/keys.json"] =
        R"({"schema_version": 1, "bindings": [{"action": "jump", "sources": {"keyboard:x": 1}}]})";
    CHECK(mentions(load(files), "g/keys.json: bindings[0].sources.keyboard:x"));
    // The path leaves the game's directory, or is not a string.
    CHECK(mentions(load_changed("g/game.json", "\"start_scene\"",
                                "\"input\": \"../keys.json\", \"start_scene\""),
                   "relative path inside the game's directory"));
    CHECK(mentions(load_changed("g/game.json", "\"start_scene\"", "\"input\": 3, \"start_scene\""),
                   "expected a string"));
}
