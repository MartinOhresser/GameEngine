#pragma once

#include "ge/json/json.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ge::module {

/// Field types a component can declare in `module.json`.
enum class FieldType : std::uint8_t { I32, U32, I64, U64, F32, F64, Bool };

[[nodiscard]] std::string_view to_string(FieldType type) noexcept;
[[nodiscard]] std::optional<FieldType> field_type_from(std::string_view name) noexcept;

/// Names of every field type, in declaration order.
[[nodiscard]] const std::vector<std::string_view>& field_type_names();

/// Names of the stages a system can run in, in execution order.
[[nodiscard]] const std::vector<std::string_view>& stage_names();

struct FieldSpec {
    std::string name;
    FieldType type = FieldType::I32;
    /// Null means "the zero of the type".
    json::Value default_value;
};

/// Whether a JSON value is acceptable for a field of this type: right kind and
/// inside the range of the type.
[[nodiscard]] bool value_fits(FieldType type, const json::Value& value);

/// A setting of the module, given a value by the game's manifest.
struct ParamSpec {
    std::string name;
    std::string description;
    FieldType type = FieldType::F32;
    json::Value default_value;
    /// Inclusive bounds, for numeric types; null when absent.
    json::Value min;
    json::Value max;
};

struct ComponentSpec {
    std::string name;
    std::string description;
    /// Whether the component is part of the state that would be replicated
    /// between peers in a networked game.
    bool replicable = false;
    std::vector<FieldSpec> fields;
};

struct SystemSpec {
    std::string name;
    std::string description;
    std::string stage;
    /// Components the system reads and writes: `Name` for the module's own,
    /// `module.Name` for a dependency's.
    std::vector<std::string> reads;
    std::vector<std::string> writes;
    /// Systems that must run before (`after`) or after (`before`) this one:
    /// `name` for the module's own, `module.name` for a dependency's.
    std::vector<std::string> after;
    std::vector<std::string> before;
};

/// The content of one `module.json`.
struct ModuleSpec {
    std::string name;
    std::string version;
    std::string description;
    std::vector<std::string> depends;
    std::vector<ParamSpec> params;
    std::vector<ComponentSpec> components;
    std::vector<SystemSpec> systems;
};

/// A problem found in a file: where (`path` inside the document) and why.
struct Diagnostic {
    std::string file;
    std::string path;
    std::string message;

    /// "file: path: message".
    [[nodiscard]] std::string to_string() const;
};

struct LoadResult {
    ModuleSpec spec;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

/// Parses and validates the text of a `module.json`. `file` labels the
/// diagnostics. Checks everything that can be checked in a single file: shapes,
/// names, types, defaults, and references to the module's own components and
/// systems.
[[nodiscard]] LoadResult load_module(std::string_view text, std::string_view file);

/// Checks a set of modules against each other: unique names, existing and
/// acyclic dependencies, and references to other modules' components and
/// systems (which must come from a declared dependency). `files[i]` labels the
/// diagnostics of `modules[i]`.
[[nodiscard]] std::vector<Diagnostic> check_modules(const std::vector<ModuleSpec>& modules,
                                                    const std::vector<std::string>& files);

/// The catalog entry for a module: what Claude reads to pick modules.
[[nodiscard]] json::Value catalog_entry(const ModuleSpec& module);

} // namespace ge::module
