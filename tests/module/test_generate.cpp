#include "ge/module/generate.hpp"

#include "../support/mini_test.hpp"

#include <string>

using ge::module::generate;
using ge::module::GeneratedFile;
using ge::module::load_module;

namespace {

const GeneratedFile* find_file(const std::vector<GeneratedFile>& files, const char* suffix) {
    for (const GeneratedFile& file : files) {
        const std::string& path = file.path;
        const std::string tail = suffix;
        if (path.size() >= tail.size() &&
            path.compare(path.size() - tail.size(), tail.size(), tail) == 0) {
            return &file;
        }
    }
    return nullptr;
}

bool contains(const std::string& text, const char* fragment) {
    return text.find(fragment) != std::string::npos;
}

std::vector<GeneratedFile> generate_from(const std::string& text) {
    const auto result = load_module(text, "x");
    CHECK(result.ok());
    return generate(result.spec);
}

} // namespace

TEST_CASE("generate: component structs carry their defaults") {
    const auto files = generate_from(R"({
      "schema_version": 1, "name": "demo", "version": "1.2.3",
      "components": [
        {"name": "Everything", "description": "One of each.\nTwo lines.", "replicable": true, "fields": [
          {"name": "a", "type": "i32", "default": -5},
          {"name": "b", "type": "u32", "default": 7},
          {"name": "c", "type": "i64", "default": -9},
          {"name": "d", "type": "u64", "default": 11},
          {"name": "e", "type": "f32", "default": 2},
          {"name": "f", "type": "f64", "default": 0.5},
          {"name": "g", "type": "bool", "default": true},
          {"name": "h", "type": "bool"},
          {"name": "i", "type": "f32", "default": 1.25},
          {"name": "j", "type": "i32", "default": -2147483648},
          {"name": "k", "type": "i64", "default": -9223372036854775808},
          {"name": "l", "type": "f32"}
        ]},
        {"name": "Marker", "replicable": false}
      ]
    })");
    const GeneratedFile* header = find_file(files, "ge/mod/demo/demo.gen.hpp");
    REQUIRE(header != nullptr);
    const std::string& text = header->content;
    CHECK(contains(text, "namespace ge::mod::demo {"));
    CHECK(contains(text, "/// One of each. Two lines.\n"));
    CHECK(contains(text, "std::int32_t a = -5;"));
    CHECK(contains(text, "std::uint32_t b = 7U;"));
    CHECK(contains(text, "std::int64_t c = -9LL;"));
    CHECK(contains(text, "std::uint64_t d = 11ULL;"));
    CHECK(contains(text, "float e = 2.0F;"));
    CHECK(contains(text, "double f = 0.5;"));
    CHECK(contains(text, "bool g = true;"));
    CHECK(contains(text, "bool h = false;"));
    CHECK(contains(text, "float i = 1.25F;"));
    CHECK(contains(text, "std::int32_t j = (-2147483647 - 1);"));
    CHECK(contains(text, "std::int64_t k = (-9223372036854775807LL - 1);"));
    CHECK(contains(text, "float l = 0.0F;"));
    CHECK(contains(text, "hasher.add(a);"));
    CHECK(contains(text, "struct Marker {"));
    CHECK(contains(text, "void hash(::ge::Hasher& /*hasher*/) const {}"));
}

TEST_CASE("generate: systems are declared and registered with their ordering") {
    const auto files = generate_from(R"({
      "schema_version": 1, "name": "demo", "version": "1.2.3", "description": "d",
      "components": [{"name": "C", "replicable": false}],
      "systems": [
        {"name": "first", "description": "Goes first.", "stage": "PreUpdate", "reads": ["C"]},
        {"name": "second", "stage": "Update", "writes": ["C"], "after": ["first"],
         "before": ["other.last"]}
      ]
    })");
    const GeneratedFile* header = find_file(files, "demo.gen.hpp");
    const GeneratedFile* source = find_file(files, "demo.gen.cpp");
    REQUIRE(header != nullptr);
    REQUIRE(source != nullptr);
    CHECK(contains(header->content, "/// Goes first.\n/// Runs in PreUpdate.\n"));
    CHECK(contains(header->content, "void first(::ge::ecs::World& world);"));
    CHECK(contains(header->content, "void second(::ge::ecs::World& world);"));
    const std::string& cpp = source->content;
    CHECK(contains(cpp, "app.add_module(info());"));
    CHECK(contains(cpp, "app.add_system(::ge::app::Stage::PreUpdate, &first, \"demo.first\");"));
    CHECK(contains(cpp, "::ge::app::Order::between({\"demo.first\"}, {\"other.last\"})"));
    CHECK(contains(cpp, "system_0_reads{\"demo.C\"}"));
    CHECK(contains(cpp, "system_1_writes{\"demo.C\"}"));
    CHECK(contains(cpp, "\"demo\", \"1.2.3\", depends, components, systems"));
}

TEST_CASE("generate: a module with nothing in it still generates") {
    const auto files =
        generate_from(R"({"schema_version": 1, "name": "empty", "version": "0.0.1"})");
    const GeneratedFile* source = find_file(files, "empty.gen.cpp");
    REQUIRE(source != nullptr);
    CHECK(contains(source->content, "std::array<::ge::app::ComponentInfo, 0> components{};"));
    CHECK(contains(source->content, "std::array<::ge::app::SystemInfo, 0> systems{};"));
    CHECK(contains(source->content, "std::array<std::string_view, 0> depends{};"));
}

TEST_CASE("generate: output is deterministic and ships a formatting opt-out") {
    const char* text = R"({"schema_version": 1, "name": "demo", "version": "1.0.0",
        "components": [{"name": "C", "replicable": false, "fields": [{"name": "x", "type": "f32"}]}]})";
    const auto first = generate_from(text);
    const auto second = generate_from(text);
    REQUIRE(first.size() == second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        CHECK(first[i].path == second[i].path);
        CHECK(first[i].content == second[i].content);
    }
    const GeneratedFile* format = find_file(first, ".clang-format");
    REQUIRE(format != nullptr);
    CHECK(format->content == "DisableFormat: true\n");
}
