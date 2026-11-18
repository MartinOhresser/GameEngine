// The game's own named lists, and fields that name one item ("ref") or a set of
// items ("ref_set") of them. A small module made for the test has the fields.
#include "ge/app/app.hpp"
#include "ge/game/game.hpp"
#include "ge/game/runtime.hpp"

#include "../support/mini_test.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <string>

using ge::game::FileReader;
using ge::game::ModuleEntry;

namespace {

struct Bag {
    std::uint32_t kind = 0;
    std::uint32_t accepts = 0;
    std::uint32_t plain = 0;
};

void insert_bag(ge::ecs::World& world, ge::ecs::Entity entity,
                std::span<const ge::app::FieldValue> values) {
    Bag bag;
    for (const ge::app::FieldValue& field : values) {
        const auto number = ge::app::scalar_as<std::uint32_t>(field.value);
        if (field.name == "kind") {
            bag.kind = number;
        } else if (field.name == "accepts") {
            bag.accepts = number;
        } else {
            bag.plain = number;
        }
    }
    static_cast<void>(world.insert(entity, bag));
}

constexpr std::array<ge::app::FieldInfo, 3> bag_fields{{
    {"kind", "u32", "item_kind", false},
    {"accepts", "u32", "category", true},
    {"plain", "u32", "", false},
}};
constexpr std::array<ge::app::ComponentInfo, 1> bag_components{
    {{"Bag", false, bag_fields, &insert_bag, nullptr}}};
const ge::app::ModuleInfo bag_info{"bags", "0.1.0", {}, bag_components, {}, {}, nullptr};

const ge::app::ModuleInfo& info() {
    return bag_info;
}
void install(ge::app::App& app) {
    app.add_module(info());
}

const std::array<ModuleEntry, 1> linked = {{{&info, &install}}};

using Files = std::map<std::string, std::string>;

Files sample() {
    return {
        {"g/game.json", R"({
            "schema_version": 1, "name": "bags", "modules": ["bags"],
            "lists": {
                "item_kind": ["potion", "sword", "key"],
                "category": ["weapon", "consumable", "tool"],
            },
            "prefabs": ["prefabs/bag.json"], "scenes": ["scenes/main.json"],
            "start_scene": "main",
        })"},
        {"g/prefabs/bag.json", R"({"schema_version": 1, "name": "bag",
            "components": {"bags.Bag": {"kind": "sword", "accepts": ["weapon", "tool"]}}})"},
        {"g/scenes/main.json", R"({"schema_version": 1, "name": "main", "entities": [
            {"name": "weapons", "prefab": "bag"},
            {"name": "potions", "prefab": "bag", "components": {"bags.Bag": {
                "kind": "potion", "accepts": "consumable"}}},
            {"name": "anything", "prefab": "bag", "components": {"bags.Bag": {
                "kind": 2, "accepts": 7, "plain": 9}}},
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

Bag bag_of(ge::app::App& app, const ge::game::SetupResult& setup, const char* name) {
    const Bag* bag = app.world().get<Bag>(setup.find(name));
    CHECK(bag != nullptr);
    return bag != nullptr ? *bag : Bag{99, 99, 99};
}

} // namespace

TEST_CASE("lists: the game declares its own lists, in order") {
    const auto loaded = load(sample());
    REQUIRE(loaded.ok());
    const std::vector<std::string> kinds = loaded.game.ref_names("item_kind");
    REQUIRE(kinds.size() == 3U);
    CHECK(kinds[0] == "potion");
    CHECK(kinds[2] == "key");
    CHECK(loaded.game.ref_names("category").size() == 3U);
    CHECK(loaded.game.ref_names("nothing").empty());
}

TEST_CASE("lists: a name is its index, a set of names is a bit mask") {
    const auto loaded = load(sample());
    REQUIRE(loaded.ok());
    ge::app::App app;
    const ge::game::SetupResult setup = ge::game::setup_game(app, loaded.game, linked);
    REQUIRE(setup.ok());
    const Bag weapons = bag_of(app, setup, "weapons");
    CHECK(weapons.kind == 1U);    // sword
    CHECK(weapons.accepts == 5U); // weapon (bit 0) and tool (bit 2)
    const Bag potions = bag_of(app, setup, "potions");
    CHECK(potions.kind == 0U);    // potion
    CHECK(potions.accepts == 2U); // a single name is a set of one: consumable, bit 1
    const Bag anything = bag_of(app, setup, "anything");
    CHECK(anything.kind == 2U); // numbers stay numbers
    CHECK(anything.accepts == 7U);
    CHECK(anything.plain == 9U);
}

TEST_CASE("lists: an unknown name says what the list holds") {
    const auto loaded = load_changed("g/prefabs/bag.json", "\"sword\"", "\"swrod\"");
    CHECK(!loaded.ok());
    CHECK(mentions(loaded, "no item_kind is named \"swrod\""));
    CHECK(mentions(loaded, "did you mean \"sword\""));
    CHECK(mentions(loaded, "potion, sword, key"));
    const auto in_set = load_changed("g/prefabs/bag.json", "\"tool\"", "\"tol\"");
    CHECK(mentions(in_set, "no category is named \"tol\""));
}

TEST_CASE("lists: only a set takes several names, and a number must be in range") {
    CHECK(mentions(load_changed("g/prefabs/bag.json", "\"sword\"", "[\"sword\"]"), "expected"));
    CHECK(mentions(load_changed("g/prefabs/bag.json", "\"sword\"", "3"),
                   "there is no item_kind number 3"));
    CHECK(mentions(load_changed("g/prefabs/bag.json", "[\"weapon\", \"tool\"]", "[\"weapon\", 4]"),
                   "expected a category name"));
}

TEST_CASE("lists: a list the game does not declare is an error where it is used") {
    const auto loaded =
        load_changed("g/game.json", "\"item_kind\": [\"potion\", \"sword\", \"key\"],", "");
    CHECK(!loaded.ok());
    CHECK(mentions(loaded, "the game declares none"));
}

TEST_CASE("lists: the manifest's lists are checked") {
    CHECK(mentions(load_changed("g/game.json", "\"item_kind\":", "\"Item Kind\":"),
                   "lower_snake_case"));
    CHECK(mentions(load_changed("g/game.json", "\"item_kind\":", "\"atlas\":"),
                   "made by the manifest"));
    CHECK(mentions(
        load_changed("g/game.json", "[\"potion\", \"sword\", \"key\"]", "[\"potion\", \"potion\"]"),
        "already in this list"));
    CHECK(mentions(load_changed("g/game.json", "[\"potion\", \"sword\", \"key\"]", "[\"Potion\"]"),
                   "lower_snake_case"));
    CHECK(mentions(load_changed("g/game.json", "[\"potion\", \"sword\", \"key\"]", "\"potion\""),
                   "expected an array"));
}

TEST_CASE("lists: a set holds at most 32 names") {
    std::string names = "[";
    for (int i = 0; i < 33; ++i) {
        names += (i == 0 ? "" : ",") + std::string("\"c") + std::to_string(i) + "\"";
    }
    names += "]";
    Files files = sample();
    std::string& manifest = files["g/game.json"];
    manifest.replace(manifest.find("[\"weapon\", \"consumable\", \"tool\"]"),
                     std::string("[\"weapon\", \"consumable\", \"tool\"]").size(), names);
    files["g/prefabs/bag.json"] = R"({"schema_version": 1, "name": "bag",
        "components": {"bags.Bag": {"accepts": ["c32"]}}})";
    const auto loaded = load(files);
    CHECK(mentions(loaded, "past the first 32"));
}
