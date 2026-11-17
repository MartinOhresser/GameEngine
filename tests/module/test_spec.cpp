#include "ge/app/stage.hpp"
#include "ge/module/spec.hpp"

#include "../support/mini_test.hpp"

#include <algorithm>
#include <string>

using ge::module::check_modules;
using ge::module::Diagnostic;
using ge::module::load_module;
using ge::module::ModuleSpec;

namespace {

const char* const minimal = R"({
  "schema_version": 1, "name": "demo", "version": "1.0.0",
  "components": [
    {"name": "Health", "replicable": true,
     "fields": [{"name": "points", "type": "i32", "default": 10}]}
  ],
  "systems": [
    {"name": "heal", "stage": "Update", "reads": ["Health"], "writes": ["Health"]}
  ]
})";

bool mentions(const std::vector<Diagnostic>& diagnostics, const char* fragment) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [fragment](const Diagnostic& d) {
        return d.to_string().find(fragment) != std::string::npos;
    });
}

bool load_fails(const std::string& text, const char* fragment) {
    return mentions(load_module(text, "demo/module.json").diagnostics, fragment);
}

/// The minimal module with one piece replaced.
std::string with(const char* from, const char* to) {
    std::string text = minimal;
    const std::size_t at = text.find(from);
    if (at == std::string::npos) {
        return "NOT FOUND: " + std::string(from);
    }
    return text.replace(at, std::string(from).size(), to);
}

} // namespace

TEST_CASE("module: a valid module loads") {
    const auto result = load_module(minimal, "demo/module.json");
    REQUIRE(result.ok());
    CHECK(result.spec.name == "demo");
    CHECK(result.spec.version == "1.0.0");
    REQUIRE(result.spec.components.size() == 1U);
    CHECK(result.spec.components[0].replicable);
    REQUIRE(result.spec.components[0].fields.size() == 1U);
    CHECK(result.spec.components[0].fields[0].default_value.as_int() == 10);
    REQUIRE(result.spec.systems.size() == 1U);
    CHECK(result.spec.systems[0].stage == "Update");
}

TEST_CASE("module: the stage names match the engine's stages") {
    const auto& names = ge::module::stage_names();
    REQUIRE(names.size() == ge::app::stage_count);
    for (std::size_t i = 0; i < names.size(); ++i) {
        CHECK(names[i] == ge::app::to_string(static_cast<ge::app::Stage>(i)));
    }
}

TEST_CASE("module: field types round-trip through their names") {
    for (const std::string_view name : ge::module::field_type_names()) {
        const auto type = ge::module::field_type_from(name);
        REQUIRE(type.has_value());
        CHECK(ge::module::to_string(*type) == name);
    }
    CHECK(!ge::module::field_type_from("string").has_value());
}

TEST_CASE("module: problems in the JSON itself") {
    CHECK(load_fails("{", "invalid JSON at"));
    CHECK(load_fails("[]", "expected an object"));
    CHECK(load_fails("{}", "missing required key \"schema_version\""));
    CHECK(load_fails("{}", "missing required key \"name\""));
    CHECK(load_fails("{}", "missing required key \"version\""));
}

TEST_CASE("module: header problems") {
    CHECK(load_fails(with("\"schema_version\": 1", "\"schema_version\": 2"),
                     "only schema version 1"));
    CHECK(load_fails(with("\"name\": \"demo\"", "\"name\": \"Demo\""), "lower_snake_case"));
    CHECK(load_fails(with("\"name\": \"demo\"", "\"name\": \"class\""), "reserved C++ word"));
    CHECK(load_fails(with("\"name\": \"demo\"", "\"name\": 5"), "expected a string"));
    CHECK(load_fails(with("\"1.0.0\"", "\"1.0\""), "not a version"));
    CHECK(load_fails(with("\"1.0.0\"", "\"1.0.x\""), "not a version"));
    CHECK(load_fails(with("\"1.0.0\"", "\"1..0\""), "not a version"));
    CHECK(load_fails(with("\"schema_version\": 1,", "\"schema_version\": 1, \"colour\": 1,"),
                     "unknown key (expected one of"));
}

TEST_CASE("module: dependency list problems") {
    CHECK(load_fails(
        with("\"version\": \"1.0.0\",", "\"version\": \"1.0.0\", \"depends\": [\"a\", \"a\"],"),
        "listed twice"));
    CHECK(load_fails(
        with("\"version\": \"1.0.0\",", "\"version\": \"1.0.0\", \"depends\": [\"demo\"],"),
        "depend on itself"));
    CHECK(load_fails(with("\"version\": \"1.0.0\",", "\"version\": \"1.0.0\", \"depends\": [1],"),
                     "expected a string"));
    CHECK(load_fails(with("\"version\": \"1.0.0\",", "\"version\": \"1.0.0\", \"depends\": 1,"),
                     "expected an array"));
}

TEST_CASE("module: component problems") {
    CHECK(load_fails(with("\"Health\"", "\"health\""), "PascalCase"));
    CHECK(load_fails(with("\"replicable\": true,", ""), "missing required key \"replicable\""));
    CHECK(load_fails(with("\"replicable\": true", "\"replicable\": 1"), "true or false"));
    CHECK(load_fails(with("\"components\": [", "\"components\": [1, "), "expected an object"));
    CHECK(load_fails(with("\"components\": [",
                          "\"components\": [{\"name\": \"Health\", \"replicable\": false}, "),
                     "two components are named"));
    CHECK(
        load_fails(with("\"components\": [", "\"components\": 5, \"x\": ["), "expected an array"));
}

TEST_CASE("module: field problems") {
    CHECK(load_fails(with("\"points\"", "\"Points\""), "lower_snake_case"));
    CHECK(load_fails(with("\"type\": \"i32\"", "\"type\": \"int\""), "unknown type \"int\""));
    CHECK(load_fails(with("\"type\": \"i32\"", "\"type\": 3"), "expected a string"));
    CHECK(load_fails(with("\"default\": 10", "\"default\": 1.5"), "does not fit the type i32"));
    CHECK(load_fails(with("\"default\": 10", "\"default\": 3000000000"), "does not fit"));
    CHECK(load_fails(
        with("\"fields\": [", "\"fields\": [{\"name\": \"points\", \"type\": \"bool\"}, "),
        "two fields are named"));
    CHECK(load_fails(with("\"fields\": [", "\"fields\": [3, "), "expected an object"));
    CHECK(load_fails(with("\"fields\": [", "\"fields\": 3, \"x\": ["), "expected an array"));
}

TEST_CASE("module: defaults must fit their type") {
    const auto field = [](const char* type, const char* value) {
        return std::string(
                   R"({"schema_version":1,"name":"d","version":"1.0.0","components":[{"name":"C","replicable":false,"fields":[{"name":"f","type":")") +
               type + R"(","default":)" + value + "}]}]}";
    };
    CHECK(load_module(field("bool", "true"), "f").ok());
    CHECK(!load_module(field("bool", "1"), "f").ok());
    CHECK(load_module(field("u32", "4294967295"), "f").ok());
    CHECK(!load_module(field("u32", "-1"), "f").ok());
    CHECK(!load_module(field("u32", "4294967296"), "f").ok());
    CHECK(load_module(field("i64", "-9000000000"), "f").ok());
    CHECK(load_module(field("u64", "5"), "f").ok());
    CHECK(!load_module(field("u64", "-5"), "f").ok());
    CHECK(load_module(field("f32", "1.5"), "f").ok());
    CHECK(load_module(field("f32", "3"), "f").ok());
    CHECK(!load_module(field("f32", "1e39"), "f").ok());
    CHECK(load_module(field("f64", "1e39"), "f").ok());
    CHECK(!load_module(field("f64", "\"x\""), "f").ok());
}

TEST_CASE("module: system problems") {
    CHECK(load_fails(with("\"heal\"", "\"Heal\""), "lower_snake_case"));
    CHECK(load_fails(with("\"Update\"", "\"Draw\""), "unknown stage \"Draw\""));
    CHECK(load_fails(with("\"reads\": [\"Health\"]", "\"reads\": [\"Mana\"]"),
                     "no component named \"Mana\""));
    CHECK(load_fails(with("\"writes\": [\"Health\"]", "\"writes\": [\"Mana\"]"),
                     "no component named \"Mana\""));
    CHECK(load_fails(with("\"writes\": [\"Health\"]", "\"after\": [\"nope\"]"),
                     "no system named \"nope\""));
    CHECK(load_fails(with("\"writes\": [\"Health\"]", "\"before\": [\"nope\"]"),
                     "no system named \"nope\""));
    CHECK(load_fails(with("\"stage\": \"Update\",", ""), "missing required key \"stage\""));
    CHECK(load_fails(with("\"systems\": [", "\"systems\": [4, "), "expected an object"));
    CHECK(load_fails(
        with("\"systems\": [", "\"systems\": [{\"name\": \"heal\", \"stage\": \"Update\"}, "),
        "two systems are named"));
    CHECK(load_fails(with("\"systems\": [", "\"systems\": 1, \"x\": ["), "expected an array"));
    // Qualified references are left for the cross-module check.
    CHECK(!load_fails(with("\"writes\": [\"Health\"]", "\"after\": [\"other.thing\"]"), "other"));
}

TEST_CASE("module: diagnostics name the file and the place") {
    const auto result = load_module(with(R"("type": "i32")", R"("type": "int")"), "m/module.json");
    REQUIRE(!result.ok());
    CHECK(result.diagnostics[0].file == "m/module.json");
    CHECK(result.diagnostics[0].path == "components[0].fields[0].type");
    CHECK(result.diagnostics[0].to_string().find("m/module.json: components[0].fields[0].type: ") ==
          0U);
    const Diagnostic top{"f.json", "", "bad"};
    CHECK(top.to_string() == "f.json: bad");
}

namespace {

ModuleSpec load_ok(const std::string& text) {
    auto result = load_module(text, "x");
    CHECK(result.ok());
    return std::move(result.spec);
}

std::string module_text(const std::string& name, const std::string& depends,
                        const std::string& extra_system_fields = "") {
    return R"({"schema_version":1,"name":")" + name + R"(","version":"1.0.0","depends":[)" +
           depends +
           R"(],"components":[{"name":"C","replicable":false}],"systems":[{"name":"s","stage":"Update")" +
           extra_system_fields + "}]}";
}

} // namespace

TEST_CASE("modules: duplicates, unknown and cyclic dependencies") {
    const ModuleSpec a = load_ok(module_text("a", "\"b\""));
    const ModuleSpec b = load_ok(module_text("b", "\"a\""));
    const ModuleSpec c = load_ok(module_text("c", "\"missing\""));
    CHECK(mentions(check_modules({a, b}, {"a", "b"}), "dependency cycle"));
    CHECK(mentions(check_modules({c}, {"c"}), "unknown module \"missing\""));
    const ModuleSpec d = load_ok(module_text("d", ""));
    CHECK(mentions(check_modules({d, d}, {"d1", "d2"}), "already named \"d\""));
    CHECK(check_modules({d}, {"d"}).empty());
}

TEST_CASE("modules: qualified references must come from declared dependencies") {
    const ModuleSpec base = load_ok(module_text("base", ""));
    const ModuleSpec good = load_ok(module_text(
        "good", "\"base\"",
        R"(,"reads":["base.C"],"after":["base.s"],"before":["base.s"],"writes":["base.C"])"));
    CHECK(check_modules({base, good}, {"base", "good"}).empty());

    const ModuleSpec undeclared = load_ok(module_text("undeclared", "", R"(,"reads":["base.C"])"));
    CHECK(mentions(check_modules({base, undeclared}, {"base", "undeclared"}),
                   "not listed in depends"));

    const ModuleSpec no_component =
        load_ok(module_text("no_component", "\"base\"", R"(,"reads":["base.Nope"])"));
    CHECK(mentions(check_modules({base, no_component}, {"base", "no_component"}),
                   "has no component named \"Nope\""));

    const ModuleSpec no_system =
        load_ok(module_text("no_system", "\"base\"", R"(,"after":["base.nope"])"));
    CHECK(mentions(check_modules({base, no_system}, {"base", "no_system"}),
                   "has no system named \"nope\""));

    // The dependency is declared but does not exist: only the dependency is reported.
    const ModuleSpec ghost = load_ok(module_text("ghost", "\"gone\"", R"(,"reads":["gone.C"])"));
    const auto problems = check_modules({ghost}, {"ghost"});
    CHECK(problems.size() == 1U);
}

TEST_CASE("modules: the catalog entry lists everything a reader needs") {
    const auto result = load_module(minimal, "x");
    const auto entry = ge::module::catalog_entry(result.spec);
    CHECK(entry.find("name")->as_string() == "demo");
    CHECK(
        entry.find("components")->items()[0].find("fields")->items()[0].find("default")->as_int() ==
        10);
    CHECK(entry.find("systems")->items()[0].find("stage")->as_string() == "Update");
    CHECK(entry.find("depends")->items().empty());
}

namespace {

const char* const with_params = R"({
  "schema_version": 1, "name": "demo", "version": "1.0.0",
  "params": [
    {"name": "gravity", "description": "Pull.", "type": "f32", "default": 9.5, "min": 0, "max": 20},
    {"name": "lives", "type": "u32", "default": 3},
    {"name": "hard_mode", "type": "bool", "default": false}
  ]
})";

} // namespace

TEST_CASE("module: params load with their bounds") {
    const auto result = load_module(with_params, "demo/module.json");
    REQUIRE(result.ok());
    REQUIRE(result.spec.params.size() == 3U);
    CHECK(result.spec.params[0].name == "gravity");
    CHECK(result.spec.params[0].type == ge::module::FieldType::F32);
    CHECK(result.spec.params[0].default_value.as_real() == 9.5);
    CHECK(result.spec.params[0].min.as_real() == 0.0);
    CHECK(result.spec.params[0].max.as_real() == 20.0);
    CHECK(result.spec.params[1].min.is_null());
    const auto entry = ge::module::catalog_entry(result.spec);
    const auto& first = entry.find("params")->items()[0];
    CHECK(first.find("max")->as_real() == 20.0);
    CHECK(entry.find("params")->items()[1].find("max") == nullptr);
}

TEST_CASE("module: param problems") {
    const auto fails = [](const char* from, const char* to, const char* fragment) {
        std::string text = with_params;
        const std::size_t at = text.find(from);
        REQUIRE(at != std::string::npos);
        text.replace(at, std::string(from).size(), to);
        CHECK(load_fails(text, fragment));
    };
    fails(R"("name": "gravity")", R"("name": "Gravity")", "lower_snake_case");
    fails(R"("name": "lives")", R"("name": "gravity")", "two params are named");
    fails(R"("type": "f32", "default": 9.5)", R"("type": "f32")",
          "missing required key \"default\"");
    fails(R"("default": 9.5)", R"("default": 25)", "outside min and max");
    fails(R"("default": 9.5)", R"("default": -1)", "outside min and max");
    fails(R"("default": 9.5)", R"("default": true)", "does not fit the type f32");
    fails(R"("min": 0)", R"("min": 30)", "min is greater than max");
    fails(R"("max": 20)", R"("max": "x")", "does not fit the type f32");
    fails(R"("type": "bool", "default": false)", R"("type": "bool", "default": false, "min": 0)",
          "a bool has no bounds");
    fails(R"("type": "u32")", R"("type": "int")", "unknown type \"int\"");
    fails(R"("type": "u32")", R"("type": 3)", "expected a string");
    fails(R"({"name": "lives")", R"({"colour": 1, "name": "lives")", "unknown key");
    CHECK(load_fails(R"({"schema_version": 1, "name": "d", "version": "1.0.0", "params": [4]})",
                     "expected an object"));
    CHECK(load_fails(R"({"schema_version": 1, "name": "d", "version": "1.0.0", "params": 4})",
                     "expected an array"));
    CHECK(load_fails(
        R"({"schema_version": 1, "name": "d", "version": "1.0.0", "params": [{"name": "x", "type": "f32", "default": 0, "min": 1, "max": 2}]})",
        "outside min and max"));
}

TEST_CASE("module: value_fits checks kind and range") {
    using ge::json::Value;
    using ge::module::FieldType;
    using ge::module::value_fits;
    CHECK(value_fits(FieldType::I32, Value::integer(-5)));
    CHECK(!value_fits(FieldType::I32, Value::real(1.5)));
    CHECK(!value_fits(FieldType::U32, Value::integer(-1)));
    CHECK(value_fits(FieldType::F32, Value::integer(3)));
    CHECK(value_fits(FieldType::Bool, Value::boolean(true)));
    CHECK(!value_fits(FieldType::Bool, Value::integer(1)));
}
