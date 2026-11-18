#pragma once

#include "ge/app/stage.hpp"
#include "ge/ecs/world.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

namespace ge::app {

/// A value read from a game file: JSON booleans, integers and numbers.
using Scalar = std::variant<bool, std::int64_t, double>;

/// Converts a `Scalar` to the C++ type of a field. Game files are validated
/// against the field types before they are loaded, so this only converts.
template <typename T>
[[nodiscard]] T scalar_as(const Scalar& value) {
    if (const auto* flag = std::get_if<bool>(&value)) {
        return static_cast<T>(*flag);
    }
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return static_cast<T>(*integer);
    }
    return static_cast<T>(*std::get_if<double>(&value));
}

/// One field of a component or of the settings, with the value a game file gives it.
struct FieldValue {
    std::string_view name;
    Scalar value;
};

/// A field of a component, as declared in `module.json`.
struct FieldInfo {
    std::string_view name;
    std::string_view type; ///< "i32", "f32", "bool"...
    std::string_view ref;  ///< Name of the list a u32 field indexes (see `FieldSpec::ref`), or "".
};

/// A setting of a module (`params` in `module.json`), with its bounds.
struct ParamInfo {
    std::string_view name;
    std::string_view type;
    double min = std::numeric_limits<double>::lowest();
    double max = std::numeric_limits<double>::max();
};

/// Creates a component on an entity from the fields a game file gave; fields
/// that are missing keep the default declared in `module.json`.
using ComponentInsert = void (*)(ecs::World&, ecs::Entity, std::span<const FieldValue>);

/// Reads one field of a component as a `Scalar`. Returns nothing when the
/// entity does not have the component or the component has no such field.
using ComponentRead = std::optional<Scalar> (*)(const ecs::World&, ecs::Entity, std::string_view);

/// Sets the module's settings (its `Params` resource) from a game file.
using ParamsApply = void (*)(ecs::World&, std::span<const FieldValue>);

struct ComponentInfo {
    std::string_view name;
    bool replicable = false;
    std::span<const FieldInfo> fields;
    ComponentInsert insert = nullptr;
    ComponentRead read = nullptr;
};

struct SystemInfo {
    std::string_view name; ///< Qualified: "module.system".
    Stage stage = Stage::Update;
    std::span<const std::string_view> reads;  ///< Qualified: "module.Component".
    std::span<const std::string_view> writes; ///< Qualified: "module.Component".
};

/// What a module is, as generated from its `module.json`. A module's
/// generated `info()` returns one; `App::add_module` records it.
struct ModuleInfo {
    std::string_view name;
    std::string_view version;
    std::span<const std::string_view> depends;
    std::span<const ComponentInfo> components;
    std::span<const SystemInfo> systems;
    std::span<const ParamInfo> params;
    ParamsApply apply_params = nullptr;
};

} // namespace ge::app
