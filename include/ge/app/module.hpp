#pragma once

#include "ge/app/stage.hpp"

#include <span>
#include <string_view>

namespace ge::app {

/// A field of a component, as declared in `module.json`.
struct FieldInfo {
    std::string_view name;
    std::string_view type; ///< "i32", "f32", "bool"...
};

struct ComponentInfo {
    std::string_view name;
    bool replicable = false;
    std::span<const FieldInfo> fields;
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
};

} // namespace ge::app
