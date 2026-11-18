#include "ge/game/runtime.hpp"
#include "ge/game/schema.hpp"
#include "ge/mod/lifetime/lifetime.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "../support/mini_test.hpp"

#include <array>

namespace {

const std::array<ge::game::ModuleEntry, 2> linked = {{
    {&ge::mod::transform2d::info, &ge::mod::transform2d::install},
    {&ge::mod::lifetime::info, &ge::mod::lifetime::install},
}};

const ge::json::Value* schema_of(const std::vector<ge::game::SchemaFile>& files, const char* path) {
    for (const auto& file : files) {
        if (file.path == path) {
            return &file.schema;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("schema: one file per kind of game file") {
    const auto files = ge::game::editor_schemas(ge::game::catalog_of(linked));
    REQUIRE(files.size() == 5U);
    CHECK(schema_of(files, "game.schema.json") != nullptr);
    CHECK(schema_of(files, "prefab.schema.json") != nullptr);
    CHECK(schema_of(files, "scene.schema.json") != nullptr);
    CHECK(schema_of(files, "scenario.schema.json") != nullptr);
    CHECK(schema_of(files, "input.schema.json") != nullptr);
}

TEST_CASE("schema: the manifest offers the modules and the params with their bounds") {
    const auto files = ge::game::editor_schemas(ge::game::catalog_of(linked));
    const auto* game = schema_of(files, "game.schema.json");
    REQUIRE(game != nullptr);
    const auto* modules = game->find("properties")->find("modules")->find("items")->find("enum");
    REQUIRE(modules != nullptr);
    CHECK(modules->items().size() == 2U);
    CHECK(modules->items()[0].as_string() == "transform2d");
    const auto* scale = game->find("properties")
                            ->find("params")
                            ->find("properties")
                            ->find("transform2d")
                            ->find("properties")
                            ->find("speed_scale");
    REQUIRE(scale != nullptr);
    CHECK(scale->find("type")->as_string() == "number");
    CHECK(scale->find("minimum")->as_real() == 0.0);
    CHECK(scale->find("maximum")->as_real() == 10.0);
    CHECK(game->find("required")->items().size() == 3U);
}

TEST_CASE("schema: prefabs and scenes list every component with typed fields") {
    const auto files = ge::game::editor_schemas(ge::game::catalog_of(linked));
    const auto* prefab = schema_of(files, "prefab.schema.json");
    REQUIRE(prefab != nullptr);
    const auto* components = prefab->find("properties")->find("components")->find("properties");
    const auto* position = components->find("transform2d.Position");
    REQUIRE(position != nullptr);
    CHECK(position->find("properties")->find("x")->find("type")->as_string() == "number");
    CHECK(position->find("additionalProperties")->as_bool() == false);
    const auto* ticks = components->find("lifetime.Lifetime")->find("properties")->find("ticks");
    CHECK(ticks->find("type")->as_string() == "integer");
    CHECK(ticks->find("maximum")->as_int() == 2147483647);

    const auto* scene = schema_of(files, "scene.schema.json");
    REQUIRE(scene != nullptr);
    const auto* entity = scene->find("properties")->find("entities")->find("items");
    CHECK(entity->find("properties")->find("prefab") != nullptr);
    CHECK(entity->find("properties")
              ->find("components")
              ->find("properties")
              ->find("lifetime.Lifetime") != nullptr);
}

TEST_CASE("schema: scenarios list their inputs and accept both forms of a check") {
    const auto files = ge::game::editor_schemas(ge::game::catalog_of(linked));
    const auto* scenario = schema_of(files, "scenario.schema.json");
    REQUIRE(scenario != nullptr);
    const auto* properties = scenario->find("properties");
    CHECK(properties->find("ticks")->find("minimum")->as_int() == 1);
    const auto* input = properties->find("inputs")->find("items");
    CHECK(input->find("properties")->find("action") != nullptr);
    CHECK(input->find("required")->items().size() == 2U);
    CHECK(properties->find("expect")->find("items")->find("oneOf")->items().size() == 2U);
    // The manifest lists scenario files.
    const auto* game = schema_of(files, "game.schema.json");
    CHECK(game->find("properties")->find("scenarios") != nullptr);
}

TEST_CASE("schema: the input file only accepts kind:name sources with numeric scales") {
    const auto files = ge::game::editor_schemas(ge::game::catalog_of(linked));
    const auto* input = schema_of(files, "input.schema.json");
    REQUIRE(input != nullptr);
    const auto* sources = input->find("properties")
                              ->find("bindings")
                              ->find("items")
                              ->find("properties")
                              ->find("sources");
    REQUIRE(sources != nullptr);
    CHECK(sources->find("propertyNames")->find("pattern")->as_string().find("key|mouse") !=
          std::string::npos);
    CHECK(sources->find("additionalProperties")->find("type")->as_string() == "number");
}
