#include "ge/module/spec.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstddef>
#include <limits>
#include <map>
#include <set>

namespace ge::module {

namespace {

constexpr std::array<std::string_view, 7> field_type_table = {"i32", "u32", "i64", "u64",
                                                              "f32", "f64", "bool"};

const std::vector<std::string_view> stage_table = {"Startup", "PreUpdate", "FixedUpdate", "Update",
                                                   "PostUpdate"};

constexpr auto reserved_words = std::to_array<std::string_view>(
    {"and",      "auto",     "bool",     "break",  "case",     "catch",     "char",      "class",
     "const",    "continue", "default",  "delete", "do",       "double",    "else",      "enum",
     "explicit", "export",   "extern",   "false",  "float",    "for",       "friend",    "goto",
     "if",       "inline",   "int",      "long",   "main",     "mutable",   "namespace", "new",
     "not",      "nullptr",  "operator", "or",     "private",  "protected", "public",    "register",
     "return",   "short",    "signed",   "sizeof", "static",   "std",       "struct",    "switch",
     "template", "this",     "throw",    "true",   "try",      "typedef",   "typename",  "union",
     "unsigned", "using",    "virtual",  "void",   "volatile", "while",     "xor",       "app",
     "ecs",      "ge",       "json",     "mod",    "module",   "core"});

bool is_lower(char c) {
    return c >= 'a' && c <= 'z';
}
bool is_upper(char c) {
    return c >= 'A' && c <= 'Z';
}
bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

/// lower_snake_case: a letter, then letters, digits and underscores.
bool is_snake(std::string_view text) {
    return !text.empty() && is_lower(text.front()) &&
           std::all_of(text.begin(), text.end(),
                       [](char c) { return is_lower(c) || is_digit(c) || c == '_'; });
}

/// PascalCase: a capital, then letters and digits.
bool is_pascal(std::string_view text) {
    return !text.empty() && is_upper(text.front()) &&
           std::all_of(text.begin(), text.end(),
                       [](char c) { return is_lower(c) || is_upper(c) || is_digit(c); });
}

bool is_semver(std::string_view text) {
    int dots = 0;
    bool digit_seen = false;
    for (const char c : text) {
        if (is_digit(c)) {
            digit_seen = true;
        } else if (c == '.' && digit_seen && dots < 2) {
            ++dots;
            digit_seen = false;
        } else {
            return false;
        }
    }
    return dots == 2 && digit_seen;
}

bool is_reserved(std::string_view text) {
    return std::find(reserved_words.begin(), reserved_words.end(), text) != reserved_words.end();
}

std::string_view kind_name(const json::Value& value) {
    switch (value.kind()) {
    case json::Value::Kind::Null:
        return "null";
    case json::Value::Kind::Bool:
        return "a boolean";
    case json::Value::Kind::Int:
        return "an integer";
    case json::Value::Kind::Real:
        return "a number";
    case json::Value::Kind::String:
        return "a string";
    case json::Value::Kind::Array:
        return "an array";
    case json::Value::Kind::Object:
        return "an object";
    }
    return "a value"; // GCOVR_EXCL_LINE: unreachable for valid enum values
}

std::string join(const std::vector<std::string_view>& words) {
    std::string result;
    for (const std::string_view word : words) {
        if (!result.empty()) {
            result += ", ";
        }
        result += word;
    }
    return result;
}

/// Collects diagnostics for one file.
class Reader {
public:
    Reader(std::string_view file, std::vector<Diagnostic>& out) : file_(file), out_(&out) {}

    void error(const std::string& path, std::string message) {
        out_->push_back(Diagnostic{std::string(file_), path, std::move(message)});
    }

    /// Reports every member of `object` that is not in `allowed`.
    void reject_unknown(const json::Value& object, const std::string& path,
                        const std::vector<std::string_view>& allowed) {
        for (const auto& member : object.members()) {
            if (std::find(allowed.begin(), allowed.end(), member.first) == allowed.end()) {
                error(join_path(path, member.first),
                      "unknown key (expected one of: " + join(allowed) + ")");
            }
        }
    }

    /// The object, or null after reporting that it is something else.
    const json::Value* object_at(const json::Value& value, const std::string& path) {
        if (!value.is_object()) {
            error(path, "expected an object, found " + std::string(kind_name(value)));
            return nullptr;
        }
        return &value;
    }

    /// A required or optional string member. Returns whether it is present and valid.
    bool string_at(const json::Value& object, const char* key, const std::string& path,
                   bool required, std::string& out) {
        const json::Value* value = object.find(key);
        if (value == nullptr) {
            if (required) {
                error(path, std::string("missing required key \"") + key + "\"");
            }
            return false;
        }
        if (!value->is_string()) {
            error(join_path(path, key),
                  "expected a string, found " + std::string(kind_name(*value)));
            return false;
        }
        out = value->as_string();
        return true;
    }

    bool bool_at(const json::Value& object, const char* key, const std::string& path, bool required,
                 bool& out) {
        const json::Value* value = object.find(key);
        if (value == nullptr) {
            if (required) {
                error(path, std::string("missing required key \"") + key + "\"");
            }
            return false;
        }
        if (!value->is_bool()) {
            error(join_path(path, key),
                  "expected true or false, found " + std::string(kind_name(*value)));
            return false;
        }
        out = value->as_bool();
        return true;
    }

    /// An optional array of strings.
    std::vector<std::string> strings_at(const json::Value& object, const char* key,
                                        const std::string& path) {
        std::vector<std::string> result;
        const json::Value* value = object.find(key);
        if (value == nullptr) {
            return result;
        }
        const std::string array_path = join_path(path, key);
        if (!value->is_array()) {
            error(array_path, "expected an array, found " + std::string(kind_name(*value)));
            return result;
        }
        for (std::size_t i = 0; i < value->items().size(); ++i) {
            const json::Value& item = value->items()[i];
            if (!item.is_string()) {
                error(index_path(array_path, i),
                      "expected a string, found " + std::string(kind_name(item)));
            } else {
                result.push_back(item.as_string());
            }
        }
        return result;
    }

    /// An optional array member; null when absent or not an array.
    const json::Value* array_at(const json::Value& object, const char* key,
                                const std::string& path) {
        const json::Value* value = object.find(key);
        if (value == nullptr) {
            return nullptr;
        }
        if (!value->is_array()) {
            error(join_path(path, key),
                  "expected an array, found " + std::string(kind_name(*value)));
            return nullptr;
        }
        return value;
    }

    static std::string join_path(const std::string& base, std::string_view key) {
        return base.empty() ? std::string(key) : base + "." + std::string(key);
    }

    static std::string index_path(const std::string& base, std::size_t index) {
        return base + "[" + std::to_string(index) + "]";
    }

private:
    std::string_view file_;
    std::vector<Diagnostic>* out_;
};

void check_name(Reader& reader, const std::string& path, const std::string& name, bool pascal) {
    const bool shaped = pascal ? is_pascal(name) : is_snake(name);
    if (!shaped) {
        reader.error(path, "\"" + name + "\" must be " +
                               (pascal ? "PascalCase (letters and digits, starting with a capital)"
                                       : "lower_snake_case (lowercase letters, digits and "
                                         "underscores, starting with a letter)"));
    } else if (is_reserved(name)) {
        reader.error(path, "\"" + name + "\" is a reserved C++ word");
    }
}

bool default_fits(FieldType type, const json::Value& value) {
    switch (type) {
    case FieldType::Bool:
        return value.is_bool();
    case FieldType::I32:
        return value.is_int() && value.as_int() >= std::numeric_limits<std::int32_t>::min() &&
               value.as_int() <= std::numeric_limits<std::int32_t>::max();
    case FieldType::U32:
        return value.is_int() && value.as_int() >= 0 &&
               value.as_int() <= std::numeric_limits<std::uint32_t>::max();
    case FieldType::I64:
        return value.is_int();
    case FieldType::U64:
        return value.is_int() && value.as_int() >= 0;
    case FieldType::F32:
        return value.is_number() && value.as_real() >= -static_cast<double>(FLT_MAX) &&
               value.as_real() <= static_cast<double>(FLT_MAX);
    case FieldType::F64:
        return value.is_number();
    }
    return false; // GCOVR_EXCL_LINE: unreachable for valid enum values
}

void read_field(Reader& reader, const json::Value& value, const std::string& path,
                ComponentSpec& component) {
    const json::Value* object = reader.object_at(value, path);
    if (object == nullptr) {
        return;
    }
    reader.reject_unknown(*object, path, {"name", "type", "default"});
    FieldSpec field;
    const bool has_name = reader.string_at(*object, "name", path, true, field.name);
    if (has_name) {
        check_name(reader, Reader::join_path(path, "name"), field.name, false);
        const bool duplicate =
            std::any_of(component.fields.begin(), component.fields.end(),
                        [&field](const FieldSpec& other) { return other.name == field.name; });
        if (duplicate) {
            reader.error(Reader::join_path(path, "name"),
                         "two fields are named \"" + field.name + "\"");
        }
    }
    std::string type_name;
    if (reader.string_at(*object, "type", path, true, type_name)) {
        const std::optional<FieldType> type = field_type_from(type_name);
        if (!type) {
            reader.error(Reader::join_path(path, "type"),
                         "unknown type \"" + type_name +
                             "\" (expected one of: " + join(field_type_names()) + ")");
        } else {
            field.type = *type;
            if (const json::Value* def = object->find("default"); def != nullptr) {
                if (default_fits(*type, *def)) {
                    field.default_value = *def;
                } else {
                    reader.error(Reader::join_path(path, "default"),
                                 "the default does not fit the type " + type_name);
                }
            }
        }
    }
    component.fields.push_back(std::move(field));
}

void read_component(Reader& reader, const json::Value& value, const std::string& path,
                    ModuleSpec& module) {
    const json::Value* object = reader.object_at(value, path);
    if (object == nullptr) {
        return;
    }
    reader.reject_unknown(*object, path, {"name", "description", "replicable", "fields"});
    ComponentSpec component;
    if (reader.string_at(*object, "name", path, true, component.name)) {
        check_name(reader, Reader::join_path(path, "name"), component.name, true);
        const bool duplicate = std::any_of(
            module.components.begin(), module.components.end(),
            [&component](const ComponentSpec& other) { return other.name == component.name; });
        if (duplicate) {
            reader.error(Reader::join_path(path, "name"),
                         "two components are named \"" + component.name + "\"");
        }
    }
    static_cast<void>(reader.string_at(*object, "description", path, false, component.description));
    static_cast<void>(reader.bool_at(*object, "replicable", path, true, component.replicable));
    if (const json::Value* fields = reader.array_at(*object, "fields", path); fields != nullptr) {
        const std::string fields_path = Reader::join_path(path, "fields");
        for (std::size_t i = 0; i < fields->items().size(); ++i) {
            read_field(reader, fields->items()[i], Reader::index_path(fields_path, i), component);
        }
    }
    module.components.push_back(std::move(component));
}

void read_system(Reader& reader, const json::Value& value, const std::string& path,
                 ModuleSpec& module) {
    const json::Value* object = reader.object_at(value, path);
    if (object == nullptr) {
        return;
    }
    reader.reject_unknown(*object, path,
                          {"name", "description", "stage", "reads", "writes", "after", "before"});
    SystemSpec system;
    if (reader.string_at(*object, "name", path, true, system.name)) {
        check_name(reader, Reader::join_path(path, "name"), system.name, false);
        const bool duplicate =
            std::any_of(module.systems.begin(), module.systems.end(),
                        [&system](const SystemSpec& other) { return other.name == system.name; });
        if (duplicate) {
            reader.error(Reader::join_path(path, "name"),
                         "two systems are named \"" + system.name + "\"");
        }
    }
    static_cast<void>(reader.string_at(*object, "description", path, false, system.description));
    if (reader.string_at(*object, "stage", path, true, system.stage)) {
        const auto& stages = stage_names();
        if (std::find(stages.begin(), stages.end(), system.stage) == stages.end()) {
            reader.error(Reader::join_path(path, "stage"),
                         "unknown stage \"" + system.stage +
                             "\" (expected one of: " + join(stages) + ")");
        }
    }
    system.reads = reader.strings_at(*object, "reads", path);
    system.writes = reader.strings_at(*object, "writes", path);
    system.after = reader.strings_at(*object, "after", path);
    system.before = reader.strings_at(*object, "before", path);
    module.systems.push_back(std::move(system));
}

void read_param(Reader& reader, const json::Value& value, const std::string& path,
                ModuleSpec& module) {
    const json::Value* object = reader.object_at(value, path);
    if (object == nullptr) {
        return;
    }
    reader.reject_unknown(*object, path, {"name", "description", "type", "default", "min", "max"});
    ParamSpec param;
    if (reader.string_at(*object, "name", path, true, param.name)) {
        check_name(reader, Reader::join_path(path, "name"), param.name, false);
        const bool duplicate =
            std::any_of(module.params.begin(), module.params.end(),
                        [&param](const ParamSpec& other) { return other.name == param.name; });
        if (duplicate) {
            reader.error(Reader::join_path(path, "name"),
                         "two params are named \"" + param.name + "\"");
        }
    }
    static_cast<void>(reader.string_at(*object, "description", path, false, param.description));
    std::string type_name;
    if (!reader.string_at(*object, "type", path, true, type_name)) {
        module.params.push_back(std::move(param));
        return;
    }
    const std::optional<FieldType> type = field_type_from(type_name);
    if (!type) {
        reader.error(Reader::join_path(path, "type"),
                     "unknown type \"" + type_name +
                         "\" (expected one of: " + join(field_type_names()) + ")");
        module.params.push_back(std::move(param));
        return;
    }
    param.type = *type;

    const auto bound = [&](const char* key, json::Value& out) {
        const json::Value* given = object->find(key);
        if (given == nullptr) {
            return;
        }
        if (*type == FieldType::Bool) {
            reader.error(Reader::join_path(path, key), "a bool has no bounds");
        } else if (!default_fits(*type, *given)) {
            reader.error(Reader::join_path(path, key),
                         std::string("the bound does not fit the type ") + type_name);
        } else {
            out = *given;
        }
    };
    bound("min", param.min);
    bound("max", param.max);
    if (param.min.is_number() && param.max.is_number() &&
        param.min.as_real() > param.max.as_real()) {
        reader.error(Reader::join_path(path, "min"), "min is greater than max");
    }

    const json::Value* def = object->find("default");
    if (def == nullptr) {
        reader.error(path, "missing required key \"default\"");
    } else if (!default_fits(*type, *def)) {
        reader.error(Reader::join_path(path, "default"),
                     "the default does not fit the type " + type_name);
    } else {
        param.default_value = *def;
        const bool too_low = param.min.is_number() && def->as_real() < param.min.as_real();
        const bool too_high = param.max.is_number() && def->as_real() > param.max.as_real();
        if (too_low || too_high) {
            reader.error(Reader::join_path(path, "default"), "the default is outside min and max");
        }
    }
    module.params.push_back(std::move(param));
}

bool is_qualified(const std::string& reference) {
    return reference.find('.') != std::string::npos;
}

/// Splits "module.Name" at the dot; the module part is empty for a bare name.
std::pair<std::string, std::string> split_reference(const std::string& reference) {
    const std::size_t dot = reference.find('.');
    if (dot == std::string::npos) {
        return {"", reference};
    }
    return {reference.substr(0, dot), reference.substr(dot + 1)};
}

bool has_component(const ModuleSpec& module, const std::string& name) {
    return std::any_of(module.components.begin(), module.components.end(),
                       [&name](const ComponentSpec& c) { return c.name == name; });
}

bool has_system(const ModuleSpec& module, const std::string& name) {
    return std::any_of(module.systems.begin(), module.systems.end(),
                       [&name](const SystemSpec& s) { return s.name == name; });
}

/// Checks the references of one module that stay inside it. Qualified ones are
/// left for `check_modules`.
void check_local_references(Reader& reader, const ModuleSpec& module) {
    for (std::size_t i = 0; i < module.systems.size(); ++i) {
        const SystemSpec& system = module.systems[i];
        const std::string base = Reader::index_path("systems", i);
        const auto check = [&](const std::vector<std::string>& names, const char* key,
                               bool components) {
            for (std::size_t j = 0; j < names.size(); ++j) {
                if (is_qualified(names[j])) {
                    continue;
                }
                const bool found =
                    components ? has_component(module, names[j]) : has_system(module, names[j]);
                if (!found) {
                    reader.error(Reader::index_path(Reader::join_path(base, key), j),
                                 std::string("this module has no ") +
                                     (components ? "component" : "system") + " named \"" +
                                     names[j] + "\"");
                }
            }
        };
        check(system.reads, "reads", true);
        check(system.writes, "writes", true);
        check(system.after, "after", false);
        check(system.before, "before", false);
    }
}

} // namespace

bool value_fits(FieldType type, const json::Value& value) {
    return default_fits(type, value);
}

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index): every enumerator has a name
std::string_view to_string(FieldType type) noexcept {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index): every enumerator has a
    // name
    return field_type_table[static_cast<std::size_t>(type)];
}

std::optional<FieldType> field_type_from(std::string_view name) noexcept {
    for (std::size_t i = 0; i < field_type_table.size(); ++i) {
        if (field_type_table[i] == name) {
            return static_cast<FieldType>(i);
        }
    }
    return std::nullopt;
}

// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)

const std::vector<std::string_view>& field_type_names() {
    static const std::vector<std::string_view> names(field_type_table.begin(),
                                                     field_type_table.end());
    return names;
}

const std::vector<std::string_view>& stage_names() {
    return stage_table;
}

std::string Diagnostic::to_string() const {
    std::string text = file;
    if (!path.empty()) {
        text += ": " + path;
    }
    return text + ": " + message;
}

LoadResult load_module(std::string_view text, std::string_view file) {
    LoadResult result;
    Reader reader(file, result.diagnostics);

    const json::ParseResult parsed = json::parse(text);
    if (parsed.error.has_value()) {
        reader.error("", "invalid JSON at " + parsed.error->to_string());
        return result;
    }
    const json::Value* root = reader.object_at(parsed.value, "");
    if (root == nullptr) {
        return result;
    }
    reader.reject_unknown(*root, "",
                          {"$schema", "schema_version", "name", "version", "description", "depends",
                           "params", "components", "systems"});

    if (const json::Value* version = root->find("schema_version"); version == nullptr) {
        reader.error("", "missing required key \"schema_version\"");
    } else if (!version->is_int() || version->as_int() != 1) {
        reader.error("schema_version", "only schema version 1 is supported");
    }

    ModuleSpec& module = result.spec;
    if (reader.string_at(*root, "name", "", true, module.name)) {
        check_name(reader, "name", module.name, false);
    }
    if (reader.string_at(*root, "version", "", true, module.version) &&
        !is_semver(module.version)) {
        reader.error("version", "\"" + module.version + "\" is not a version like 1.2.3");
    }
    static_cast<void>(reader.string_at(*root, "description", "", false, module.description));

    module.depends = reader.strings_at(*root, "depends", "");
    std::set<std::string> seen;
    for (std::size_t i = 0; i < module.depends.size(); ++i) {
        const std::string path = Reader::index_path("depends", i);
        check_name(reader, path, module.depends[i], false);
        if (!seen.insert(module.depends[i]).second) {
            reader.error(path, "\"" + module.depends[i] + "\" is listed twice");
        }
        if (module.depends[i] == module.name) {
            reader.error(path, "a module cannot depend on itself");
        }
    }

    if (const json::Value* params = reader.array_at(*root, "params", ""); params != nullptr) {
        for (std::size_t i = 0; i < params->items().size(); ++i) {
            read_param(reader, params->items()[i], Reader::index_path("params", i), module);
        }
    }
    if (const json::Value* components = reader.array_at(*root, "components", "");
        components != nullptr) {
        for (std::size_t i = 0; i < components->items().size(); ++i) {
            read_component(reader, components->items()[i], Reader::index_path("components", i),
                           module);
        }
    }
    if (const json::Value* systems = reader.array_at(*root, "systems", ""); systems != nullptr) {
        for (std::size_t i = 0; i < systems->items().size(); ++i) {
            read_system(reader, systems->items()[i], Reader::index_path("systems", i), module);
        }
    }
    check_local_references(reader, module);
    return result;
}

namespace {

/// Checks one module against the others.
class LinkCheck {
public:
    LinkCheck(const std::vector<ModuleSpec>& modules,
              const std::map<std::string, std::size_t>& by_name, Reader& reader)
        : modules_(&modules), by_name_(&by_name), reader_(&reader) {}

    void run(const ModuleSpec& module) {
        module_ = &module;
        for (std::size_t d = 0; d < module.depends.size(); ++d) {
            if (!by_name_->contains(module.depends[d])) {
                reader_->error(Reader::index_path("depends", d),
                               "unknown module \"" + module.depends[d] + "\"");
            }
        }
        for (std::size_t s = 0; s < module.systems.size(); ++s) {
            check_system(module.systems[s], Reader::index_path("systems", s));
        }
    }

private:
    void check_system(const SystemSpec& system, const std::string& base) {
        check_all(system.reads, Reader::join_path(base, "reads"), true);
        check_all(system.writes, Reader::join_path(base, "writes"), true);
        check_all(system.after, Reader::join_path(base, "after"), false);
        check_all(system.before, Reader::join_path(base, "before"), false);
    }

    void check_all(const std::vector<std::string>& names, const std::string& path, bool component) {
        for (std::size_t j = 0; j < names.size(); ++j) {
            check_reference(names[j], Reader::index_path(path, j), component);
        }
    }

    /// A qualified reference must name a declared dependency that has the item.
    void check_reference(const std::string& reference, const std::string& path, bool component) {
        const auto [owner, item] = split_reference(reference);
        if (owner.empty()) {
            return;
        }
        const auto& depends = module_->depends;
        if (std::find(depends.begin(), depends.end(), owner) == depends.end()) {
            reader_->error(path, "\"" + reference + "\" uses module \"" + owner +
                                     "\", which is not listed in depends");
            return;
        }
        const auto target = by_name_->find(owner);
        if (target == by_name_->end()) {
            return; // already reported as an unknown dependency
        }
        const ModuleSpec& other = (*modules_)[target->second];
        const bool exists = component ? has_component(other, item) : has_system(other, item);
        if (!exists) {
            reader_->error(path, "module \"" + owner + "\" has no " +
                                     (component ? "component" : "system") + " named \"" + item +
                                     "\"");
        }
    }

    const std::vector<ModuleSpec>* modules_;
    const std::map<std::string, std::size_t>* by_name_;
    Reader* reader_;
    const ModuleSpec* module_ = nullptr;
};

/// Names of the modules that are part of a dependency cycle (or depend on one
/// that cannot be resolved): repeatedly strips modules whose dependencies are gone.
std::set<std::string> modules_in_cycles(const std::vector<ModuleSpec>& modules) {
    std::set<std::string> remaining;
    for (const ModuleSpec& module : modules) {
        remaining.insert(module.name);
    }
    bool progress = true;
    while (progress) {
        progress = false;
        for (const ModuleSpec& module : modules) {
            const auto blocked = [&](const std::string& dependency) {
                return remaining.contains(dependency) && dependency != module.name;
            };
            if (remaining.contains(module.name) &&
                std::none_of(module.depends.begin(), module.depends.end(), blocked)) {
                remaining.erase(module.name);
                progress = true;
            }
        }
    }
    return remaining;
}

} // namespace

std::vector<Diagnostic> check_modules(const std::vector<ModuleSpec>& modules,
                                      const std::vector<std::string>& files) {
    std::vector<Diagnostic> found;
    std::map<std::string, std::size_t> by_name;
    for (std::size_t i = 0; i < modules.size(); ++i) {
        const auto [existing, inserted] = by_name.emplace(modules[i].name, i);
        if (!inserted) {
            std::string message = "another module is already named \"" + modules[i].name + "\" (";
            message += files[existing->second];
            message += ")";
            found.push_back(Diagnostic{files[i], "name", std::move(message)});
        }
    }

    for (std::size_t i = 0; i < modules.size(); ++i) {
        Reader reader(files[i], found);
        LinkCheck(modules, by_name, reader).run(modules[i]);
    }

    const std::set<std::string> cyclic = modules_in_cycles(modules);
    for (std::size_t i = 0; i < modules.size(); ++i) {
        if (cyclic.contains(modules[i].name)) {
            std::string message = "\"" + modules[i].name;
            message += "\" is part of a dependency cycle";
            found.push_back(Diagnostic{files[i], "depends", std::move(message)});
        }
    }
    return found;
}

json::Value catalog_entry(const ModuleSpec& module) {
    const auto strings = [](const std::vector<std::string>& items) {
        json::Value array = json::Value::array();
        for (const std::string& item : items) {
            array.push_back(json::Value::string(item));
        }
        return array;
    };

    json::Value params = json::Value::array();
    for (const ParamSpec& param : module.params) {
        json::Value entry = json::Value::object();
        entry.set("name", json::Value::string(param.name));
        entry.set("description", json::Value::string(param.description));
        entry.set("type", json::Value::string(std::string(to_string(param.type))));
        entry.set("default", param.default_value);
        if (param.min.is_number()) {
            entry.set("min", param.min);
        }
        if (param.max.is_number()) {
            entry.set("max", param.max);
        }
        params.push_back(std::move(entry));
    }

    json::Value components = json::Value::array();
    for (const ComponentSpec& component : module.components) {
        json::Value fields = json::Value::array();
        for (const FieldSpec& field : component.fields) {
            json::Value entry = json::Value::object();
            entry.set("name", json::Value::string(field.name));
            entry.set("type", json::Value::string(std::string(to_string(field.type))));
            if (!field.default_value.is_null()) {
                entry.set("default", field.default_value);
            }
            fields.push_back(std::move(entry));
        }
        json::Value entry = json::Value::object();
        entry.set("name", json::Value::string(component.name));
        entry.set("description", json::Value::string(component.description));
        entry.set("replicable", json::Value::boolean(component.replicable));
        entry.set("fields", std::move(fields));
        components.push_back(std::move(entry));
    }

    json::Value systems = json::Value::array();
    for (const SystemSpec& system : module.systems) {
        json::Value entry = json::Value::object();
        entry.set("name", json::Value::string(system.name));
        entry.set("description", json::Value::string(system.description));
        entry.set("stage", json::Value::string(system.stage));
        entry.set("reads", strings(system.reads));
        entry.set("writes", strings(system.writes));
        entry.set("after", strings(system.after));
        entry.set("before", strings(system.before));
        systems.push_back(std::move(entry));
    }

    json::Value entry = json::Value::object();
    entry.set("name", json::Value::string(module.name));
    entry.set("version", json::Value::string(module.version));
    entry.set("description", json::Value::string(module.description));
    entry.set("depends", strings(module.depends));
    entry.set("params", std::move(params));
    entry.set("components", std::move(components));
    entry.set("systems", std::move(systems));
    return entry;
}

} // namespace ge::module
