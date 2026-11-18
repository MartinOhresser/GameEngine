#pragma once

#include "ge/app/module.hpp"
#include "ge/json/json.hpp"
#include "ge/module/spec.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ge::game {

struct FieldDef {
    std::string name;
    module::FieldType type = module::FieldType::I32;
    /// The list this u32 field indexes ("atlas"), or empty; see `FieldSpec::ref`.
    std::string ref;
};

struct ComponentDef {
    std::string name;
    std::vector<FieldDef> fields;

    [[nodiscard]] const FieldDef* find_field(std::string_view field) const;
};

/// A setting of a module. `min` and `max` are null when the setting has no bound.
struct ParamDef {
    std::string name;
    module::FieldType type = module::FieldType::F32;
    json::Value min;
    json::Value max;
};

struct ModuleDef {
    std::string name;
    std::vector<std::string> depends;
    std::vector<ParamDef> params;
    std::vector<ComponentDef> components;

    [[nodiscard]] const ComponentDef* find_component(std::string_view component) const;
    [[nodiscard]] const ParamDef* find_param(std::string_view param) const;
};

/// What the available modules offer, as far as validating a game needs to know.
/// It can be built from `module.json` files (the `gectl validate` tool) or from
/// the generated `info()` of linked modules (a runner), so both check a game
/// the same way.
class Catalog {
public:
    [[nodiscard]] static Catalog from_specs(const std::vector<module::ModuleSpec>& modules);
    [[nodiscard]] static Catalog from_infos(std::span<const app::ModuleInfo* const> modules);

    [[nodiscard]] const ModuleDef* find_module(std::string_view name) const;

    /// Looks up "module.Component".
    [[nodiscard]] const ComponentDef* find_component(std::string_view qualified) const;

    [[nodiscard]] const std::vector<ModuleDef>& modules() const noexcept { return modules_; }

private:
    std::vector<ModuleDef> modules_;
};

} // namespace ge::game
