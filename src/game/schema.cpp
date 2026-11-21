#include "ge/game/schema.hpp"

#include <cstdint>
#include <limits>
#include <utility>

namespace ge::game {

namespace {

using json::Value;

Value text(const std::string& value) {
    return Value::string(value);
}

Value type_only(const char* type) {
    Value schema = Value::object();
    schema.set("type", text(type));
    return schema;
}

Value integer_between(std::int64_t low, std::int64_t high) {
    Value schema = type_only("integer");
    schema.set("minimum", Value::integer(low));
    schema.set("maximum", Value::integer(high));
    return schema;
}

/// The schema of a value of a field type.
Value type_schema(module::FieldType type) {
    switch (type) {
    case module::FieldType::Bool:
        return type_only("boolean");
    case module::FieldType::F32:
    case module::FieldType::F64:
        return type_only("number");
    case module::FieldType::I32:
        return integer_between(std::numeric_limits<std::int32_t>::lowest(),
                               std::numeric_limits<std::int32_t>::max());
    case module::FieldType::U32:
        return integer_between(0, std::numeric_limits<std::uint32_t>::max());
    case module::FieldType::I64:
        return type_only("integer");
    case module::FieldType::U64: {
        Value schema = type_only("integer");
        schema.set("minimum", Value::integer(0));
        return schema;
    }
    }
    return Value::object();
}

Value closed_object(Value properties) {
    Value schema = type_only("object");
    schema.set("properties", std::move(properties));
    schema.set("additionalProperties", Value::boolean(false));
    return schema;
}

Value component_schema(const ComponentDef& component) {
    Value fields = Value::object();
    for (const FieldDef& field : component.fields) {
        fields.set(field.name, type_schema(field.type));
    }
    return closed_object(std::move(fields));
}

/// `components`: one property per "module.Component".
Value components_schema(const Catalog& catalog) {
    Value properties = Value::object();
    for (const ModuleDef& module : catalog.modules()) {
        for (const ComponentDef& component : module.components) {
            properties.set(module.name + "." + component.name, component_schema(component));
        }
    }
    return closed_object(std::move(properties));
}

Value param_schema(const ParamDef& param) {
    Value schema = type_schema(param.type);
    if (param.min.is_number()) {
        schema.set("minimum", param.min);
    }
    if (param.max.is_number()) {
        schema.set("maximum", param.max);
    }
    return schema;
}

Value params_schema(const Catalog& catalog) {
    Value modules = Value::object();
    for (const ModuleDef& module : catalog.modules()) {
        Value params = Value::object();
        for (const ParamDef& param : module.params) {
            params.set(param.name, param_schema(param));
        }
        modules.set(module.name, closed_object(std::move(params)));
    }
    return closed_object(std::move(modules));
}

Value array_of(const Value& item) {
    Value schema = type_only("array");
    schema.set("items", item);
    return schema;
}

Value name_schema() {
    Value schema = type_only("string");
    schema.set("pattern", text("^[a-z][a-z0-9_]*$"));
    return schema;
}

Value version_schema() {
    Value schema = type_only("integer");
    schema.set("const", Value::integer(1));
    return schema;
}

Value required(std::initializer_list<const char*> keys) {
    Value list = Value::array();
    for (const char* key : keys) {
        list.push_back(text(key));
    }
    return list;
}

Value document(const char* title, Value properties, Value required_keys) {
    Value schema = closed_object(std::move(properties));
    schema.set("$schema", text("http://json-schema.org/draft-07/schema#"));
    schema.set("title", text(title));
    schema.set("required", std::move(required_keys));
    return schema;
}

Value game_schema(const Catalog& catalog) {
    Value module_names = Value::array();
    for (const ModuleDef& module : catalog.modules()) {
        module_names.push_back(text(module.name));
    }
    Value module_name = type_only("string");
    module_name.set("enum", std::move(module_names));

    Value simulation = Value::object();
    simulation.set("tick_rate", integer_between(1, 1000));
    Value seed = type_only("integer");
    seed.set("minimum", Value::integer(0));
    simulation.set("seed", std::move(seed));

    Value properties = Value::object();
    properties.set("schema_version", version_schema());
    properties.set("name", name_schema());
    properties.set("description", type_only("string"));
    properties.set("title", type_only("string"));
    Value version = type_only("string");
    version.set("pattern", text("^[0-9]+\\.[0-9]+\\.[0-9]+$"));
    properties.set("version", std::move(version));
    Value id = type_only("string");
    id.set("pattern", text("^[a-z][a-z0-9_]*(\\.[a-z][a-z0-9_]*)+$"));
    properties.set("id", std::move(id));
    properties.set("icon", type_only("string"));
    properties.set("modules", array_of(module_name));
    properties.set("simulation", closed_object(std::move(simulation)));
    Value window = Value::object();
    window.set("width", integer_between(1, 16384));
    window.set("height", integer_between(1, 16384));
    Value aspect = type_only("number");
    aspect.set("exclusiveMinimum", Value::integer(0));
    Value range = Value::object();
    range.set("min_aspect", aspect);
    range.set("max_aspect", aspect);
    Value adapt = Value::object();
    adapt.set("oneOf", Value::array({type_only("boolean"), closed_object(std::move(range))}));
    window.set("adapt", std::move(adapt));
    properties.set("window", closed_object(std::move(window)));
    properties.set("params", params_schema(catalog));
    Value list = type_only("array");
    list.set("items", name_schema());
    Value lists = type_only("object");
    lists.set("additionalProperties", std::move(list));
    properties.set("lists", std::move(lists));
    properties.set("atlases", array_of(type_only("string")));
    properties.set("prefabs", array_of(type_only("string")));
    properties.set("scenes", array_of(type_only("string")));
    properties.set("scenarios", array_of(type_only("string")));
    properties.set("input", type_only("string"));
    properties.set("start_scene", name_schema());
    return document("Game manifest", std::move(properties),
                    required({"schema_version", "name", "modules"}));
}

Value prefab_schema(const Catalog& catalog) {
    Value properties = Value::object();
    properties.set("schema_version", version_schema());
    properties.set("name", name_schema());
    properties.set("extends", name_schema());
    properties.set("components", components_schema(catalog));
    return document("Prefab", std::move(properties), required({"schema_version", "name"}));
}

Value scene_schema(const Catalog& catalog) {
    Value entity = Value::object();
    entity.set("name", name_schema());
    entity.set("prefab", name_schema());
    entity.set("components", components_schema(catalog));

    Value properties = Value::object();
    properties.set("schema_version", version_schema());
    properties.set("name", name_schema());
    properties.set("entities", array_of(closed_object(std::move(entity))));
    return document("Scene", std::move(properties),
                    required({"schema_version", "name", "entities"}));
}

/// The checks of a scenario are short sentences; the pattern only catches the
/// shapes that can never be right, `gectl validate` does the rest.
Value check_schema() {
    Value text = type_only("string");
    text.set("minLength", Value::integer(3));
    Value timed = Value::object();
    timed.set("at", integer_between(0, 10'000'000));
    timed.set("check", text);
    Value object = closed_object(std::move(timed));
    object.set("required", required({"check"}));
    Value either = Value::object();
    Value options = Value::array();
    options.push_back(text);
    options.push_back(std::move(object));
    either.set("oneOf", std::move(options));
    return either;
}

Value scenario_schema() {
    Value input = Value::object();
    input.set("tick", integer_between(0, 10'000'000));
    input.set("action", name_schema());
    input.set("value", type_only("number"));
    Value input_object = closed_object(std::move(input));
    input_object.set("required", required({"tick", "action"}));

    Value properties = Value::object();
    properties.set("schema_version", version_schema());
    properties.set("name", name_schema());
    properties.set("description", type_only("string"));
    properties.set("scene", name_schema());
    properties.set("ticks", integer_between(1, 10'000'000));
    // The screen it is played on (a phone's, say): a game that follows the screen takes its shape.
    Value screen = Value::object();
    screen.set("width", integer_between(1, 16384));
    screen.set("height", integer_between(1, 16384));
    Value screen_object = closed_object(std::move(screen));
    screen_object.set("required", required({"width", "height"}));
    properties.set("screen", std::move(screen_object));
    properties.set("inputs", array_of(input_object));
    // Run-time lists a host would fill: a name, then texts (any text, not names).
    Value filled = type_only("object");
    filled.set("additionalProperties", array_of(type_only("string")));
    properties.set("lists", std::move(filled));
    const Value check = check_schema();
    properties.set("expect", array_of(check));
    return document("Scenario", std::move(properties),
                    required({"schema_version", "name", "ticks"}));
}

/// Sources are "kind:name" in lower case; the scale of each is a number.
Value input_schema() {
    Value source_names = type_only("string");
    source_names.set("pattern", Value::string("^(key|mouse|pad|touch):[a-z0-9_]+$"));
    Value sources = type_only("object");
    sources.set("propertyNames", std::move(source_names));
    sources.set("additionalProperties", type_only("number"));
    sources.set("minProperties", Value::integer(1));

    Value binding = Value::object();
    binding.set("action", name_schema());
    binding.set("sources", std::move(sources));
    Value binding_object = closed_object(std::move(binding));
    binding_object.set("required", required({"action", "sources"}));

    Value properties = Value::object();
    properties.set("schema_version", version_schema());
    properties.set("description", type_only("string"));
    properties.set("bindings", array_of(binding_object));
    return document("Input bindings", std::move(properties),
                    required({"schema_version", "bindings"}));
}

} // namespace

std::vector<SchemaFile> editor_schemas(const Catalog& catalog) {
    std::vector<SchemaFile> files;
    files.push_back({"game.schema.json", game_schema(catalog)});
    files.push_back({"prefab.schema.json", prefab_schema(catalog)});
    files.push_back({"scene.schema.json", scene_schema(catalog)});
    files.push_back({"scenario.schema.json", scenario_schema()});
    files.push_back({"input.schema.json", input_schema()});
    return files;
}

} // namespace ge::game
