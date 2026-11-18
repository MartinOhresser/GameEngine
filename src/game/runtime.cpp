#include "ge/game/runtime.hpp"

#include "ge/app/lists.hpp"
#include "ge/app/spawn.hpp"
#include "ge/app/viewport.hpp"
#include "ge/core/random.hpp"

#include <algorithm>
#include <memory>

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
            if (auto* names = app.world().resource<app::EntityNames>(); names != nullptr) {
                names->set(data.name, entity);
            }
        }
    }
    return result;
}

namespace {

/// Whether the entity asked to stay when the scene changes: it has a
/// `scene_switch.Persistent` whose `keep` is on.
bool stays_between_scenes(const app::App& app, ecs::Entity entity) {
    const app::ComponentInfo* info = find_component_info(app, "scene_switch.Persistent");
    if (info == nullptr || info->read == nullptr) {
        return false;
    }
    const std::optional<app::Scalar> keep = info->read(app.world(), entity, "keep");
    return keep && app::scalar_as<bool>(*keep);
}

} // namespace

void change_scene(app::App& app, const Game& game, std::uint32_t scene) {
    if (scene >= game.scenes.size()) {
        return; // validated games never ask for a scene that does not exist
    }
    for (const ecs::Entity entity : app.world().entities()) {
        if (!stays_between_scenes(app, entity)) {
            static_cast<void>(app.world().despawn(entity));
        }
    }
    if (auto* names = app.world().resource<app::EntityNames>(); names != nullptr) {
        names->retain([&app](ecs::Entity entity) { return app.world().is_alive(entity); });
    }
    if (auto* requests = app.world().resource<app::SpawnRequests>(); requests != nullptr) {
        requests->queue.clear(); // they were meant for the scene that is gone
    }
    static_cast<void>(spawn_scene(app, game, game.scenes[scene].name));
    app.world().insert_resource(app::CurrentScene{scene});
}

namespace {

/// Sets the fields of `component` ("module.Component") on `entity` when the module is installed.
void put(app::App& app, ecs::Entity entity, std::string_view component,
         const std::vector<app::FieldValue>& fields) {
    const app::ComponentInfo* info = find_component_info(app, component);
    if (info != nullptr && info->insert != nullptr) {
        info->insert(app.world(), entity, fields);
    }
}

} // namespace

void spawn_prefab(app::App& app, const Game& game, const app::SpawnRequest& request) {
    const std::vector<std::string> names = game.ref_names("prefab");
    if (request.prefab >= names.size()) {
        return; // validated games only name prefabs that exist
    }
    const ecs::Entity entity = app.world().spawn();
    for (const ComponentData& component :
         resolve_components(game, names[request.prefab], {})) {
        const app::ComponentInfo* info = find_component_info(app, component.name);
        if (info != nullptr && info->insert != nullptr) {
            info->insert(app.world(), entity, to_fields(game, *info, component.fields));
        }
    }
    put(app, entity, "transform2d.Position",
        {app::FieldValue{"x", static_cast<double>(request.x)},
         app::FieldValue{"y", static_cast<double>(request.y)}});
    if (request.has_velocity) {
        put(app, entity, "transform2d.Velocity",
            {app::FieldValue{"x", static_cast<double>(request.vx)},
             app::FieldValue{"y", static_cast<double>(request.vy)}});
    }
}

app::TextTable text_table_of(const Game& game) {
    app::TextTable table;
    if (const StringsData* words = game.active_strings(); words != nullptr) {
        for (const std::string& key : game.ref_names("string")) {
            const auto found =
                std::find_if(words->entries.begin(), words->entries.end(),
                             [&key](const auto& entry) { return entry.first == key; });
            table.texts.push_back(found == words->entries.end() ? std::string() : found->second);
        }
    }
    for (const AtlasData& atlas : game.atlases) {
        table.fonts.push_back(
            app::FontInfo{app::decode_utf8(atlas.glyphs), atlas.cell_width, atlas.cell_height});
    }
    return table;
}

app::ListNames list_names_of(const Game& game) {
    app::ListNames result;
    for (const char* built_in : {"atlas", "string", "scene", "action", "field", "prefab"}) {
        result.lists.emplace_back(built_in, game.ref_names(built_in));
    }
    for (const auto& list : game.manifest.lists) {
        result.lists.emplace_back(list.first, list.second);
    }
    return result;
}

SetupResult setup_game(app::App& app, const Game& game, std::span<const ModuleEntry> available) {
    SetupResult result;
    const Manifest& manifest = game.manifest;
    app.set_tick_rate(manifest.tick_rate);
    app.world().insert_resource(Random{manifest.seed});
    app.world().insert_resource(app::Viewport{manifest.window_width, manifest.window_height});
    app.world().insert_resource(text_table_of(game));
    app.world().insert_resource(app::ActionNames{game.ref_names("action")});
    app.world().insert_resource(list_names_of(game));
    {
        // The loaders keep their own copy of the game, so they never outlive it.
        auto shared = std::make_shared<const Game>(game);
        app.set_scene_loader([shared](app::App& running, std::uint32_t scene) {
            change_scene(running, *shared, scene);
        });
        app.set_prefab_spawner([shared](app::App& running, const app::SpawnRequest& request) {
            spawn_prefab(running, *shared, request);
        });
    }

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
    for (std::size_t i = 0; i < game.scenes.size(); ++i) {
        if (game.scenes[i].name == manifest.start_scene) {
            app.world().insert_resource(app::CurrentScene{static_cast<std::uint32_t>(i)});
        }
    }
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
