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
    properties.set("modules", array_of(module_name));
    properties.set("simulation", closed_object(std::move(simulation)));
    properties.set("params", params_schema(catalog));
    properties.set("prefabs", array_of(type_only("string")));
    properties.set("scenes", array_of(type_only("string")));
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

} // namespace

std::vector<SchemaFile> editor_schemas(const Catalog& catalog) {
    std::vector<SchemaFile> files;
    files.push_back({"game.schema.json", game_schema(catalog)});
    files.push_back({"prefab.schema.json", prefab_schema(catalog)});
    files.push_back({"scene.schema.json", scene_schema(catalog)});
    return files;
}

} // namespace ge::game
