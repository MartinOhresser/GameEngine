// Creating entities from prefabs while the game runs, the names of the game's
// lists, and the data that outlives a run.
#include "ge/app/app.hpp"
#include "ge/app/lists.hpp"
#include "ge/app/save.hpp"
#include "ge/app/spawn.hpp"
#include "ge/game/game.hpp"
#include "ge/game/runtime.hpp"
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

namespace transform2d = ge::mod::transform2d;

const std::array<ModuleEntry, 3> linked = {{
    {&transform2d::info, &transform2d::install},
    {&ge::mod::collision2d::info, &ge::mod::collision2d::install},
    {&ge::mod::scene_switch::info, &ge::mod::scene_switch::install},
}};

using Files = std::map<std::string, std::string>;

Files sample() {
    return {
        {"g/game.json", R"({
            "schema_version": 1, "name": "spawning",
            "modules": ["transform2d", "collision2d", "scene_switch"],
            "lists": {"record": ["best"]},
            "prefabs": ["prefabs/rock.json", "prefabs/arrow.json"],
            "scenes": ["scenes/a.json", "scenes/b.json"],
            "start_scene": "a",
        })"},
        {"g/prefabs/rock.json", R"({"schema_version": 1, "name": "rock", "components": {
            "transform2d.Position": {"x": 1, "y": 2}}})"},
        {"g/prefabs/arrow.json", R"({"schema_version": 1, "name": "arrow", "components": {
            "transform2d.Velocity": {"x": 5}}})"},
        {"g/scenes/a.json", R"({"schema_version": 1, "name": "a", "entities": []})"},
        {"g/scenes/b.json", R"({"schema_version": 1, "name": "b", "entities": []})"},
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

} // namespace

TEST_CASE("spawn: a request creates the prefab at the given place, between two ticks") {
    const auto loaded = load(sample());
    REQUIRE(loaded.ok());
    ge::app::App app;
    REQUIRE(ge::game::setup_game(app, loaded.game, linked).ok());
    CHECK(loaded.game.ref_names("prefab") == (std::vector<std::string>{"rock", "arrow"}));

    auto* requests = app.world().resource<ge::app::SpawnRequests>();
    requests->spawn(0, 40.0F, 50.0F);
    requests->spawn_moving(1, 3.0F, 4.0F, 0.0F, -7.0F);
    CHECK(app.world().entities().empty());
    app.run_frames(1);

    CHECK(requests->queue.empty());
    const auto entities = app.world().entities();
    REQUIRE(entities.size() == 2);
    const auto* rock = app.world().get<transform2d::Position>(entities[0]);
    REQUIRE(rock != nullptr);
    CHECK(rock->x == 40.0F); // the request's place wins over the prefab's
    CHECK(rock->y == 50.0F);
    const auto* arrow = app.world().get<transform2d::Velocity>(entities[1]);
    REQUIRE(arrow != nullptr);
    CHECK(arrow->x == 0.0F); // so does the request's velocity
    CHECK(arrow->y == -7.0F);
}

TEST_CASE("spawn: a scene change drops the requests that were not served") {
    const auto loaded = load(sample());
    REQUIRE(loaded.ok());
    ge::app::App app;
    REQUIRE(ge::game::setup_game(app, loaded.game, linked).ok());
    app.world().resource<ge::app::SpawnRequests>()->spawn(0, 1.0F, 1.0F);
    ge::game::change_scene(app, loaded.game, 1);
    CHECK(app.world().resource<ge::app::SpawnRequests>()->queue.empty());
    app.run_frames(2);
    CHECK(app.world().entities().empty());
}

TEST_CASE("spawn: the game's lists are available by name") {
    const auto loaded = load(sample());
    REQUIRE(loaded.ok());
    ge::app::App app;
    REQUIRE(ge::game::setup_game(app, loaded.game, linked).ok());
    const auto* lists = app.world().resource<ge::app::ListNames>();
    REQUIRE(lists != nullptr);
    CHECK(lists->name("record", 0) == "best");
    CHECK(lists->name("prefab", 1) == "arrow");
    CHECK(lists->name("scene", 1) == "b");
    CHECK(lists->name("record", 5).empty());
    CHECK(lists->name("nothing", 0).empty());
}

TEST_CASE("spawn: prefab is a built-in list, so a game cannot declare one") {
    Files files = sample();
    files["g/game.json"] = R"({
        "schema_version": 1, "name": "spawning", "modules": ["transform2d"],
        "lists": {"prefab": ["rock"]},
        "scenes": ["scenes/a.json"], "start_scene": "a"})";
    const auto loaded = load(files);
    CHECK(!loaded.ok());
    const bool says_why = std::ranges::any_of(loaded.diagnostics, [](const auto& diagnostic) {
        return diagnostic.to_string().find("built by the engine") != std::string::npos;
    });
    CHECK(says_why);
}

TEST_CASE("save: values are kept by name, and only a real change makes the data dirty") {
    ge::app::SaveData save;
    CHECK(!save.dirty());
    CHECK(save.get("best", 7) == 7);
    CHECK(!save.has("best"));
    save.set("best", 120);
    CHECK(save.dirty());
    CHECK(save.get("best") == 120);
    save.mark_clean();
    save.set("best", 120);
    CHECK(!save.dirty());
    save.set("best", 130);
    CHECK(save.dirty());
}

TEST_CASE("save: the text form round-trips and ignores nonsense lines") {
    ge::app::SaveData save;
    save.set("best height", 1234);
    save.set("coins", -5);
    ge::app::SaveData loaded;
    loaded.from_text(save.to_text());
    CHECK(loaded.get("best height") == 1234);
    CHECK(loaded.get("coins") == -5);
    CHECK(!loaded.dirty());

    loaded.from_text("good 3\nnonsense\n 4\nbad notanumber\nlast 9");
    CHECK(loaded.get("good") == 3);
    CHECK(loaded.get("last") == 9);
    CHECK(!loaded.has("nonsense"));
    CHECK(!loaded.has("bad"));
    CHECK(!loaded.has("coins")); // replaced, not merged
}
