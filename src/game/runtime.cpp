#include "ge/game/runtime.hpp"

#include "ge/core/random.hpp"

#include <algorithm>

namespace ge::game {

namespace {

app::Scalar to_scalar(const json::Value& value) {
    if (value.is_bool()) {
        return value.as_bool();
    }
    if (value.is_int()) {
        return value.as_int();
    }
    return value.as_real();
}

/// The index of the item a game file names for a field with a `ref`.
std::int64_t ref_index(const Game& game, std::string_view ref, const std::string& name) {
    const std::vector<std::string> names = game.ref_names(ref);
    const auto found = std::find(names.begin(), names.end(), name);
    return static_cast<std::int64_t>(found - names.begin());
}

/// What a game file writes for a field with a `ref` or `ref_set`, as a number:
/// a name is its index (a set: its bit), an array of names is the set of them.
app::Scalar ref_value(const Game& game, const app::FieldInfo& field, const json::Value& value) {
    if (value.is_string()) {
        const std::int64_t index = ref_index(game, field.ref, value.as_string());
        return field.ref_set ? std::int64_t{1} << index : index;
    }
    std::int64_t mask = 0;
    for (const json::Value& item : value.items()) {
        mask |= std::int64_t{1} << ref_index(game, field.ref, item.as_string());
    }
    return mask;
}

/// The values of a JSON object as the fields generated code understands. A
/// name given for a field with a `ref` becomes the item's index.
std::vector<app::FieldValue> to_fields(const Game& game, const app::ComponentInfo& info,
                                       const json::Value& object) {
    std::vector<app::FieldValue> fields;
    fields.reserve(object.members().size());
    for (const auto& member : object.members()) {
        if (member.second.is_string() || member.second.is_array()) {
            const auto field = std::find_if(info.fields.begin(), info.fields.end(),
                                            [&member](const app::FieldInfo& candidate) {
                                                return candidate.name == member.first;
                                            });
            if (field != info.fields.end() && !field->ref.empty()) {
                fields.push_back(
                    app::FieldValue{member.first, ref_value(game, *field, member.second)});
            }
            continue;
        }
        fields.push_back(app::FieldValue{member.first, to_scalar(member.second)});
    }
    return fields;
}

const ModuleEntry* find_entry(std::span<const ModuleEntry> modules, std::string_view name) {
    const auto found =
        std::find_if(modules.begin(), modules.end(),
                     [name](const ModuleEntry& entry) { return entry.info().name == name; });
    return found == modules.end() ? nullptr : &*found;
}

/// The manifest's modules with dependencies before the modules that need them,
/// otherwise in the manifest's order.
std::vector<const ModuleEntry*> install_order(const Manifest& manifest,
                                              std::span<const ModuleEntry> available,
                                              std::vector<std::string>& problems) {
    std::vector<const ModuleEntry*> pending;
    for (const std::string& name : manifest.modules) {
        const ModuleEntry* entry = find_entry(available, name);
        if (entry == nullptr) {
            problems.push_back("module '" + name + "' is not linked into this program");
        } else {
            pending.push_back(entry);
        }
    }
    std::vector<const ModuleEntry*> ordered;
    const auto installed = [&ordered](std::string_view name) {
        return std::any_of(ordered.begin(), ordered.end(),
                           [name](const ModuleEntry* entry) { return entry->info().name == name; });
    };
    while (!pending.empty()) {
        const auto ready =
            std::find_if(pending.begin(), pending.end(), [&](const ModuleEntry* entry) {
                const auto& depends = entry->info().depends;
                return std::all_of(depends.begin(), depends.end(), installed);
            });
        if (ready == pending.end()) {
            problems.emplace_back("the modules depend on each other in a loop or on a module "
                                  "that is not listed");
            break;
        }
        ordered.push_back(*ready);
        pending.erase(ready);
    }
    return ordered;
}

} // namespace

const app::ComponentInfo* find_component_info(const app::App& app, std::string_view qualified) {
    const std::size_t dot = qualified.find('.');
    if (dot == std::string_view::npos) {
        return nullptr;
    }
    for (const app::ModuleInfo& module : app.modules()) {
        if (module.name != qualified.substr(0, dot)) {
            continue;
        }
        for (const app::ComponentInfo& component : module.components) {
            if (component.name == qualified.substr(dot + 1)) {
                return &component;
            }
        }
    }
    return nullptr;
}

Catalog catalog_of(std::span<const ModuleEntry> modules) {
    std::vector<const app::ModuleInfo*> infos;
    infos.reserve(modules.size());
    for (const ModuleEntry& entry : modules) {
        infos.push_back(&entry.info());
    }
    return Catalog::from_infos(infos);
}

ecs::Entity SetupResult::find(std::string_view name) const {
    const auto found = std::find_if(named.begin(), named.end(),
                                    [name](const auto& item) { return item.first == name; });
    return found == named.end() ? ecs::Entity{} : found->second;
}

SetupResult spawn_scene(app::App& app, const Game& game, std::string_view scene_name) {
    SetupResult result;
    const SceneData* scene = game.find_scene(scene_name);
    if (scene == nullptr) {
        result.problems.push_back("no scene named '" + std::string(scene_name) + "'");
        return result;
    }
    for (const EntityData& data : scene->entities) {
        const ecs::Entity entity = app.world().spawn();
        for (const ComponentData& component :
             resolve_components(game, data.prefab, data.components)) {
            const app::ComponentInfo* info = find_component_info(app, component.name);
            if (info == nullptr || info->insert == nullptr) {
                result.problems.push_back("component '" + component.name +
                                          "' is not available: its module is not installed");
                continue;
            }
            const std::vector<app::FieldValue> fields = to_fields(game, *info, component.fields);
            info->insert(app.world(), entity, fields);
        }
        if (!data.name.empty()) {
            result.named.emplace_back(data.name, entity);
        }
    }
    return result;
}

SetupResult setup_game(app::App& app, const Game& game, std::span<const ModuleEntry> available) {
    SetupResult result;
    const Manifest& manifest = game.manifest;
    app.set_tick_rate(manifest.tick_rate);
    app.world().insert_resource(Random{manifest.seed});

    for (const ModuleEntry* entry : install_order(manifest, available, result.problems)) {
        entry->install(app);
    }
    for (const std::string& problem : app.module_problems()) {
        result.problems.push_back(problem);
    }

    for (const ParamSetting& setting : manifest.params) {
        const ModuleEntry* entry = find_entry(available, setting.module);
        if (entry == nullptr || entry->info().apply_params == nullptr) {
            continue;
        }
        const std::vector<app::FieldValue> fields = {
            app::FieldValue{setting.name, to_scalar(setting.value)}};
        entry->info().apply_params(app.world(), fields);
    }

    if (!result.ok()) {
        return result;
    }
    SetupResult scene = spawn_scene(app, game, manifest.start_scene);
    for (std::string& problem : scene.problems) {
        result.problems.push_back(std::move(problem));
    }
    result.named = std::move(scene.named);
    return result;
}

std::vector<std::string> load_atlases(const Game& game, app::Renderer& renderer) {
    std::vector<std::string> problems;
    for (std::size_t index = 0; index < game.atlases.size(); ++index) {
        const AtlasData& atlas = game.atlases[index];
        const app::AtlasInfo info{atlas.image, atlas.cell_width, atlas.cell_height};
        std::string error;
        if (!renderer.load_atlas(static_cast<int>(index), info, error)) {
            problems.push_back("atlas \"" + atlas.name + "\": " + error);
        }
    }
    return problems;
}

} // namespace ge::game
