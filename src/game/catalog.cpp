#include "ge/game/catalog.hpp"

#include <algorithm>
#include <limits>

namespace ge::game {

const FieldDef* ComponentDef::find_field(std::string_view field) const {
    const auto found = std::find_if(fields.begin(), fields.end(),
                                    [field](const FieldDef& def) { return def.name == field; });
    return found == fields.end() ? nullptr : &*found;
}

const ComponentDef* ModuleDef::find_component(std::string_view component) const {
    const auto found =
        std::find_if(components.begin(), components.end(),
                     [component](const ComponentDef& def) { return def.name == component; });
    return found == components.end() ? nullptr : &*found;
}

const ParamDef* ModuleDef::find_param(std::string_view param) const {
    const auto found = std::find_if(params.begin(), params.end(),
                                    [param](const ParamDef& def) { return def.name == param; });
    return found == params.end() ? nullptr : &*found;
}

const ModuleDef* Catalog::find_module(std::string_view name) const {
    const auto found = std::find_if(modules_.begin(), modules_.end(),
                                    [name](const ModuleDef& def) { return def.name == name; });
    return found == modules_.end() ? nullptr : &*found;
}

const ComponentDef* Catalog::find_component(std::string_view qualified) const {
    const std::size_t dot = qualified.find('.');
    if (dot == std::string_view::npos) {
        return nullptr;
    }
    const ModuleDef* owner = find_module(qualified.substr(0, dot));
    return owner == nullptr ? nullptr : owner->find_component(qualified.substr(dot + 1));
}

Catalog Catalog::from_specs(const std::vector<module::ModuleSpec>& modules) {
    Catalog catalog;
    for (const module::ModuleSpec& spec : modules) {
        ModuleDef def;
        def.name = spec.name;
        def.depends = spec.depends;
        for (const module::ParamSpec& param : spec.params) {
            def.params.push_back(ParamDef{param.name, param.type, param.min, param.max});
        }
        for (const module::ComponentSpec& component : spec.components) {
            ComponentDef component_def;
            component_def.name = component.name;
            for (const module::FieldSpec& field : component.fields) {
                component_def.fields.push_back(
                    FieldDef{field.name, field.type, field.ref, field.ref_set});
            }
            def.components.push_back(std::move(component_def));
        }
        catalog.modules_.push_back(std::move(def));
    }
    return catalog;
}

namespace {

module::FieldType type_of(std::string_view name) {
    return module::field_type_from(name).value_or(module::FieldType::I32);
}

/// A bound from generated code, where "no bound" is the limit of `double`.
json::Value bound_of(double value) {
    const bool unbounded = value == std::numeric_limits<double>::lowest() ||
                           value == std::numeric_limits<double>::max();
    return unbounded ? json::Value{} : json::Value::real(value);
}

} // namespace

Catalog Catalog::from_infos(std::span<const app::ModuleInfo* const> modules) {
    Catalog catalog;
    for (const app::ModuleInfo* info : modules) {
        ModuleDef def;
        def.name = std::string(info->name);
        for (const std::string_view dependency : info->depends) {
            def.depends.emplace_back(dependency);
        }
        for (const app::ParamInfo& param : info->params) {
            def.params.push_back(ParamDef{std::string(param.name), type_of(param.type),
                                          bound_of(param.min), bound_of(param.max)});
        }
        for (const app::ComponentInfo& component : info->components) {
            ComponentDef component_def;
            component_def.name = std::string(component.name);
            for (const app::FieldInfo& field : component.fields) {
                component_def.fields.push_back(FieldDef{std::string(field.name),
                                                        type_of(field.type), std::string(field.ref),
                                                        field.ref_set});
            }
            def.components.push_back(std::move(component_def));
        }
        catalog.modules_.push_back(std::move(def));
    }
    return catalog;
}

} // namespace ge::game
