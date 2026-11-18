#include "ge/module/generate.hpp"

#include <cstdint>
#include <limits>

namespace ge::module {

namespace {

std::string cpp_type(FieldType type) {
    switch (type) {
    case FieldType::I32:
        return "std::int32_t";
    case FieldType::U32:
        return "std::uint32_t";
    case FieldType::I64:
        return "std::int64_t";
    case FieldType::U64:
        return "std::uint64_t";
    case FieldType::F32:
        return "float";
    case FieldType::F64:
        return "double";
    case FieldType::Bool:
        return "bool";
    }
    return {}; // GCOVR_EXCL_LINE: unreachable for valid enum values
}

/// The C++ literal for a field's default value (zero when none is given).
std::string default_literal(const FieldSpec& field) {
    const json::Value& value = field.default_value;
    switch (field.type) {
    case FieldType::Bool:
        return value.is_bool() && value.as_bool() ? "true" : "false";
    case FieldType::I32: {
        const std::int64_t number = value.is_int() ? value.as_int() : 0;
        if (number == std::numeric_limits<std::int32_t>::min()) {
            return "(-2147483647 - 1)";
        }
        return std::to_string(number);
    }
    case FieldType::U32:
        return std::to_string(value.is_int() ? value.as_int() : 0) + "U";
    case FieldType::I64: {
        const std::int64_t number = value.is_int() ? value.as_int() : 0;
        if (number == std::numeric_limits<std::int64_t>::min()) {
            return "(-9223372036854775807LL - 1)";
        }
        return std::to_string(number) + "LL";
    }
    case FieldType::U64:
        return std::to_string(value.is_int() ? value.as_int() : 0) + "ULL";
    case FieldType::F32:
        return json::format_real(static_cast<float>(value.is_number() ? value.as_real() : 0.0)) +
               "F";
    case FieldType::F64:
        return json::format_real(value.is_number() ? value.as_real() : 0.0);
    }
    return {}; // GCOVR_EXCL_LINE: unreachable for valid enum values
}

/// A description on one line, safe inside a `//` comment.
std::string comment_text(const std::string& text) {
    std::string result;
    for (const char c : text) {
        result.push_back(c == '\n' || c == '\r' ? ' ' : c);
    }
    return result;
}

std::string quoted(const std::string& text) {
    std::string result = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            result.push_back('\\');
        }
        result.push_back(c);
    }
    result.push_back('"');
    return result;
}

/// "module.Name" for a reference, qualifying the module's own bare names.
std::string qualify(const ModuleSpec& module, const std::string& reference) {
    return reference.find('.') == std::string::npos ? module.name + "." + reference : reference;
}

std::vector<std::string> qualify_all(const ModuleSpec& module,
                                     const std::vector<std::string>& references) {
    std::vector<std::string> result;
    result.reserve(references.size());
    for (const std::string& reference : references) {
        result.push_back(qualify(module, reference));
    }
    return result;
}

/// `{"a", "b"}`.
std::string braced(const std::vector<std::string>& items) {
    std::string text = "{";
    for (std::size_t i = 0; i < items.size(); ++i) {
        text += (i == 0 ? "" : ", ") + quoted(items[i]);
    }
    return text + "}";
}

/// A constexpr array of strings named `name`.
std::string string_array(const std::string& name, const std::vector<std::string>& items) {
    return "constexpr std::array<std::string_view, " + std::to_string(items.size()) + "> " + name +
           braced(items) + ";\n";
}

/// Declares `name` as a struct with the given fields: defaults, `hash` and `==`.
std::string struct_text(const std::string& name, const std::vector<FieldSpec>& fields) {
    std::string out = "struct " + name + " {\n";
    for (const FieldSpec& field : fields) {
        out += "    " + cpp_type(field.type) + " " + field.name + " = " + default_literal(field) +
               ";\n";
    }
    if (fields.empty()) {
        out += "    void hash(::ge::Hasher& /*hasher*/) const {}\n";
    } else {
        out += "\n    void hash(::ge::Hasher& hasher) const {\n";
        for (const FieldSpec& field : fields) {
            out += "        hasher.add(" + field.name + ");\n";
        }
        out += "    }\n";
    }
    out += "\n    [[nodiscard]] friend bool operator==(const " + name + "&, const " + name +
           "&) = default;\n};\n\n";
    return out;
}

/// The loop that copies the fields a game file gave into `target`.
std::string assign_fields(const std::vector<FieldSpec>& fields, const std::string& target) {
    std::string out = "    for (const ::ge::app::FieldValue& field : values) {\n";
    bool first = true;
    for (const FieldSpec& field : fields) {
        out += std::string("        ") + (first ? "if" : "} else if") +
               " (field.name == " + quoted(field.name) + ") {\n            " + target + "." +
               field.name + " = ::ge::app::scalar_as<" + cpp_type(field.type) + ">(field.value);\n";
        first = false;
    }
    if (!first) {
        out += "        }\n";
    }
    out += "    }\n";
    return out;
}

/// The C++ expression that turns `value` of a field type into an `app::Scalar`.
std::string to_scalar_expression(FieldType type, const std::string& value) {
    switch (type) {
    case FieldType::Bool:
        return "::ge::app::Scalar{static_cast<bool>(" + value + ")}";
    case FieldType::F32:
    case FieldType::F64:
        return "::ge::app::Scalar{static_cast<double>(" + value + ")}";
    case FieldType::I32:
    case FieldType::U32:
    case FieldType::I64:
    case FieldType::U64:
        break;
    }
    return "::ge::app::Scalar{static_cast<std::int64_t>(" + value + ")}";
}

/// A function that reads one field of a component by name.
std::string read_function(const ComponentSpec& component, const std::string& index) {
    std::string out = "std::optional<::ge::app::Scalar> read_component_" + index +
                      "(const ::ge::ecs::World& world, ::ge::ecs::Entity entity,\n"
                      "                      [[maybe_unused]] std::string_view field) {\n"
                      "    const " +
                      component.name + "* component = world.get<" + component.name +
                      ">(entity);\n"
                      "    if (component == nullptr) {\n        return std::nullopt;\n    }\n";
    for (const FieldSpec& field : component.fields) {
        out += "    if (field == " + quoted(field.name) + ") {\n        return " +
               to_scalar_expression(field.type, "component->" + field.name) + ";\n    }\n";
    }
    out += "    return std::nullopt;\n}\n\n";
    return out;
}

std::vector<FieldSpec> param_fields(const ModuleSpec& module) {
    std::vector<FieldSpec> fields;
    fields.reserve(module.params.size());
    for (const ParamSpec& param : module.params) {
        fields.push_back(FieldSpec{param.name, param.type, param.default_value});
    }
    return fields;
}

std::string bound_literal(const json::Value& bound, const char* absent) {
    return bound.is_number() ? json::format_real(bound.as_real()) : absent;
}

std::string stage_enum(const std::string& stage) {
    return "::ge::app::Stage::" + stage;
}

std::string banner(const ModuleSpec& module) {
    return "// Generated by gectl from modules/" + module.name +
           "/module.json. Do not edit by hand:\n// change module.json and run `gectl generate`.\n";
}

std::string header_text(const ModuleSpec& module) {
    std::string out = banner(module);
    out += "#pragma once\n\n"
           "#include \"ge/app/app.hpp\"\n"
           "#include \"ge/app/module.hpp\"\n"
           "#include \"ge/core/hash.hpp\"\n"
           "#include \"ge/ecs/world.hpp\"\n\n"
           "#include <cstdint>\n\n";
    out += "namespace ge::mod::" + module.name + " {\n\n";

    if (!module.params.empty()) {
        out += "/// Settings of the module, filled from the game's manifest. Read them with\n"
               "/// `world.resource<Params>()`.\n";
        out += struct_text("Params", param_fields(module));
    }

    for (const ComponentSpec& component : module.components) {
        if (!component.description.empty()) {
            out += "/// " + comment_text(component.description) + "\n";
        }
        out += struct_text(component.name, component.fields);
    }

    for (const SystemSpec& system : module.systems) {
        if (!system.description.empty()) {
            out += "/// " + comment_text(system.description) + "\n";
        }
        out += "/// Runs in " + system.stage + ".\n";
        out += "void " + system.name + "(::ge::ecs::World& world);\n\n";
    }

    out += "/// What this module declares, for the catalog and for `App::add_module`.\n"
           "[[nodiscard]] const ::ge::app::ModuleInfo& info() noexcept;\n\n"
           "/// Records the module in the app and registers its systems.\n"
           "void install(::ge::app::App& app);\n\n";
    out += "} // namespace ge::mod::" + module.name + "\n";
    return out;
}

std::string components_text(const ModuleSpec& module) {
    std::string out;
    for (std::size_t i = 0; i < module.components.size(); ++i) {
        const ComponentSpec& component = module.components[i];
        const std::string index = std::to_string(i);
        out += "void insert_component_" + index +
               "(::ge::ecs::World& world, ::ge::ecs::Entity entity,\n"
               "                      [[maybe_unused]] std::span<const ::ge::app::FieldValue> "
               "values) {\n"
               "    " +
               component.name + " component;\n" + assign_fields(component.fields, "component") +
               "    static_cast<void>(world.insert(entity, std::move(component)));\n}\n\n";
        out += read_function(component, index);
        out += "constexpr std::array<::ge::app::FieldInfo, " +
               std::to_string(component.fields.size()) + "> component_" + index + "_fields{";
        for (std::size_t f = 0; f < component.fields.size(); ++f) {
            const FieldSpec& field = component.fields[f];
            out += (f == 0 ? "{{" : ", {") + quoted(field.name) + ", " +
                   quoted(std::string(to_string(field.type))) + "}";
        }
        out += component.fields.empty() ? "};\n\n" : "}};\n\n";
    }
    out += "constexpr std::array<::ge::app::ComponentInfo, " +
           std::to_string(module.components.size()) + "> components{";
    for (std::size_t i = 0; i < module.components.size(); ++i) {
        const std::string index = std::to_string(i);
        out += i == 0 ? "{{" : ", {";
        out += quoted(module.components[i].name);
        out += module.components[i].replicable ? ", true, component_" : ", false, component_";
        out += index;
        out += "_fields, insert_component_";
        out += index;
        out += ", read_component_";
        out += index;
        out += "}";
    }
    out += module.components.empty() ? "};\n\n" : "}};\n\n";
    return out;
}

std::string systems_text(const ModuleSpec& module) {
    std::string out;
    for (std::size_t i = 0; i < module.systems.size(); ++i) {
        const SystemSpec& system = module.systems[i];
        out += string_array("system_" + std::to_string(i) + "_reads",
                            qualify_all(module, system.reads));
        out += string_array("system_" + std::to_string(i) + "_writes",
                            qualify_all(module, system.writes));
    }
    out += "constexpr std::array<::ge::app::SystemInfo, " + std::to_string(module.systems.size()) +
           "> systems{";
    for (std::size_t i = 0; i < module.systems.size(); ++i) {
        const SystemSpec& system = module.systems[i];
        out += (i == 0 ? "{{" : ", {") + quoted(module.name + "." + system.name) + ", " +
               stage_enum(system.stage) + ", system_" + std::to_string(i) + "_reads, system_" +
               std::to_string(i) + "_writes}";
    }
    out += module.systems.empty() ? "};\n\n" : "}};\n\n";
    return out;
}

std::string params_text(const ModuleSpec& module) {
    std::string out = "constexpr std::array<::ge::app::ParamInfo, " +
                      std::to_string(module.params.size()) + "> params{";
    for (std::size_t i = 0; i < module.params.size(); ++i) {
        const ParamSpec& param = module.params[i];
        out += (i == 0 ? "{{" : ", {") + quoted(param.name) + ", " +
               quoted(std::string(to_string(param.type))) + ", " +
               bound_literal(param.min, "std::numeric_limits<double>::lowest()") + ", " +
               bound_literal(param.max, "std::numeric_limits<double>::max()") + "}";
    }
    out += module.params.empty() ? "};\n" : "}};\n";
    if (!module.params.empty()) {
        out += "\nvoid apply_params(::ge::ecs::World& world, std::span<const "
               "::ge::app::FieldValue> values) {\n"
               "    Params* target = world.resource<Params>();\n"
               "    if (target == nullptr) {\n        return;\n    }\n" +
               assign_fields(param_fields(module), "(*target)") + "}\n";
    }
    return out;
}

std::string install_text(const ModuleSpec& module) {
    std::string out = "void install(::ge::app::App& app) {\n    app.add_module(info());\n";
    if (!module.params.empty()) {
        out += "    app.world().insert_resource(Params{});\n";
    }
    for (const SystemSpec& system : module.systems) {
        out += "    app.add_system(" + stage_enum(system.stage) + ", &" + system.name + ", " +
               quoted(module.name + "." + system.name);
        if (!system.after.empty() || !system.before.empty()) {
            out += ", ::ge::app::Order::between(" + braced(qualify_all(module, system.after)) +
                   ", " + braced(qualify_all(module, system.before)) + ")";
        }
        out += ");\n";
    }
    return out + "}\n";
}

std::string source_text(const ModuleSpec& module) {
    std::string out = banner(module);
    out += "#include \"ge/mod/" + module.name + "/" + module.name +
           ".gen.hpp\"\n\n"
           "#include <array>\n#include <limits>\n#include <optional>\n#include <span>\n#include "
           "<string_view>\n"
           "#include <utility>\n\n"
           "// NOLINTBEGIN\nnamespace ge::mod::" +
           module.name + " {\n\nnamespace {\n\n";
    out += components_text(module) + systems_text(module);
    out += string_array("depends", module.depends) + params_text(module);
    out += "\n} // namespace\n\n"
           "const ::ge::app::ModuleInfo& info() noexcept {\n"
           "    static const ::ge::app::ModuleInfo value{" +
           quoted(module.name) + ", " + quoted(module.version) +
           ", depends, components, systems, params, " +
           (module.params.empty() ? std::string("nullptr") : std::string("&apply_params")) +
           "};\n    return value;\n}\n\n";
    out += install_text(module);
    out += "\n} // namespace ge::mod::" + module.name + "\n// NOLINTEND\n";
    return out;
}

} // namespace

std::vector<GeneratedFile> generate(const ModuleSpec& module) {
    std::vector<GeneratedFile> files;
    files.push_back({".clang-format", "DisableFormat: true\n"});
    files.push_back(
        {"ge/mod/" + module.name + "/" + module.name + ".gen.hpp", header_text(module)});
    files.push_back({module.name + ".gen.cpp", source_text(module)});
    return files;
}

} // namespace ge::module
