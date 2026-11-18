#include "ge/game/game.hpp"

#include "check.hpp"
#include "scenario_read.hpp"

#include <algorithm>
#include <set>

namespace ge::game {

namespace {

using namespace detail;

void read_component_fields(Checker& checker, const json::Value& value, const std::string& path,
                           const ComponentDef& component, ComponentData& out) {
    const json::Value* fields = checker.object(value, path);
    if (fields == nullptr) {
        return;
    }
    const std::vector<std::string> names = names_of(component.fields);
    for (const auto& member : fields->members()) {
        const FieldDef* field = component.find_field(member.first);
        const std::string field_path = join_path(path, member.first);
        if (field == nullptr) {
            checker.error(field_path,
                          "unknown field" + suggestion(member.first, names) +
                              (names.empty() ? " (this component has no fields)"
                                             : " (expected one of: " + join(names) + ")"));
            continue;
        }
        check_value(checker, field_path, field->type, member.second);
    }
    out.fields = *fields;
}

/// Reads the `components` object of a prefab or an entity.
std::vector<ComponentData> read_components(const Context& context, Checker& checker,
                                           const json::Value& value, const std::string& path) {
    std::vector<ComponentData> result;
    const json::Value* object = checker.object(value, path);
    if (object == nullptr) {
        return result;
    }
    std::vector<std::string> known;
    for (const std::string& module_name : context.modules) {
        const ModuleDef* module = context.catalog->find_module(module_name);
        if (module == nullptr) {
            continue;
        }
        for (const ComponentDef& component : module->components) {
            known.push_back(module_name + "." + component.name);
        }
    }
    for (const auto& member : object->members()) {
        const std::string& key = member.first;
        const std::string component_path = join_path(path, key);
        const std::size_t dot = key.find('.');
        if (dot == std::string::npos) {
            checker.error(component_path,
                          "a component is written \"module.Component\"" + suggestion(key, known));
            continue;
        }
        const std::string module_name = key.substr(0, dot);
        if (std::find(context.modules.begin(), context.modules.end(), module_name) ==
            context.modules.end()) {
            checker.error(component_path,
                          "module \"" + module_name + "\" is not listed in the manifest's modules");
            continue;
        }
        const ComponentDef* component = context.catalog->find_component(key);
        if (component == nullptr) {
            checker.error(component_path, "unknown component" + suggestion(key, known) +
                                              " (known: " + join(known) + ")");
            continue;
        }
        ComponentData data;
        data.name = key;
        read_component_fields(checker, member.second, component_path, *component, data);
        result.push_back(std::move(data));
    }
    return result;
}

// ---- manifest --------------------------------------------------------------

void read_simulation(Checker& checker, const json::Value& root, Manifest& manifest) {
    const json::Value* simulation = root.find("simulation");
    if (simulation == nullptr) {
        return;
    }
    const json::Value* object = checker.object(*simulation, "simulation");
    if (object == nullptr) {
        return;
    }
    checker.reject_unknown(*object, "simulation", {"tick_rate", "seed"});
    if (const json::Value* rate = object->find("tick_rate"); rate != nullptr) {
        if (rate->is_int() && rate->as_int() >= 1 && rate->as_int() <= 1000) {
            manifest.tick_rate = static_cast<std::uint32_t>(rate->as_int());
        } else {
            checker.error("simulation.tick_rate", "expected a whole number from 1 to 1000");
        }
    }
    if (const json::Value* seed = object->find("seed"); seed != nullptr) {
        if (seed->is_int() && seed->as_int() >= 0) {
            manifest.seed = static_cast<std::uint64_t>(seed->as_int());
        } else {
            checker.error("simulation.seed", "expected a whole number, zero or more");
        }
    }
}

void read_modules(const Context& context, Checker& checker, const json::Value& root,
                  Manifest& manifest) {
    manifest.modules = checker.texts(root, "modules", "");
    if (root.find("modules") == nullptr) {
        checker.error("", "missing required key \"modules\"");
    }
    std::vector<std::string> available;
    for (const ModuleDef& module : context.catalog->modules()) {
        available.push_back(module.name);
    }
    std::set<std::string> seen;
    for (std::size_t i = 0; i < manifest.modules.size(); ++i) {
        const std::string& name = manifest.modules[i];
        const std::string path = index_path("modules", i);
        if (!seen.insert(name).second) {
            checker.error(path, "\"" + name + "\" is listed twice");
        }
        const ModuleDef* module = context.catalog->find_module(name);
        if (module == nullptr) {
            checker.error(path, "unknown module \"" + name + "\"" + suggestion(name, available) +
                                    " (available: " + join(available) + ")");
            continue;
        }
        for (const std::string& dependency : module->depends) {
            if (std::find(manifest.modules.begin(), manifest.modules.end(), dependency) ==
                manifest.modules.end()) {
                std::string message = "module \"";
                message += name;
                message += "\" needs \"";
                message += dependency;
                message += "\", which is not listed in modules";
                checker.error(path, message);
            }
        }
    }
}

/// The settings of one module in `params`, checked against the module's declarations.
void read_module_params(Checker& checker, const ModuleDef& module, const json::Value& settings,
                        const std::string& module_path, Manifest& manifest) {
    const std::vector<std::string> names = names_of(module.params);
    for (const auto& setting : settings.members()) {
        const std::string path = join_path(module_path, setting.first);
        const ParamDef* param = module.find_param(setting.first);
        if (param == nullptr) {
            checker.error(path, "module \"" + module.name + "\" has no param named \"" +
                                    setting.first + "\"" + suggestion(setting.first, names) +
                                    (names.empty() ? " (it has no params)"
                                                   : " (expected one of: " + join(names) + ")"));
            continue;
        }
        if (!module::value_fits(param->type, setting.second)) {
            check_value(checker, path, param->type, setting.second);
            continue;
        }
        const bool below = param->min.is_number() && setting.second.is_number() &&
                           setting.second.as_real() < param->min.as_real();
        const bool above = param->max.is_number() && setting.second.is_number() &&
                           setting.second.as_real() > param->max.as_real();
        if (below || above) {
            checker.error(path, "the value is outside the bounds of the param");
            continue;
        }
        manifest.params.push_back(ParamSetting{module.name, setting.first, setting.second});
    }
}

void read_params(const Context& context, Checker& checker, const json::Value& root,
                 Manifest& manifest) {
    const json::Value* params = root.find("params");
    if (params == nullptr) {
        return;
    }
    const json::Value* by_module = checker.object(*params, "params");
    if (by_module == nullptr) {
        return;
    }
    for (const auto& module_entry : by_module->members()) {
        const std::string module_path = join_path("params", module_entry.first);
        const ModuleDef* module = context.catalog->find_module(module_entry.first);
        const bool listed = std::find(manifest.modules.begin(), manifest.modules.end(),
                                      module_entry.first) != manifest.modules.end();
        if (module == nullptr || !listed) {
            checker.error(module_path,
                          "\"" + module_entry.first + "\" is not one of the manifest's modules");
            continue;
        }
        if (const json::Value* settings = checker.object(module_entry.second, module_path);
            settings != nullptr) {
            read_module_params(checker, *module, *settings, module_path, manifest);
        }
    }
}

/// A list of file paths in the manifest, each checked to stay inside the game.
std::vector<std::string> read_file_list(Checker& checker, const json::Value& root,
                                        const char* key) {
    std::vector<std::string> files = checker.texts(root, key, "");
    for (std::size_t i = 0; i < files.size(); ++i) {
        if (!is_safe_relative(files[i])) {
            checker.error(index_path(key, i),
                          "\"" + files[i] +
                              "\" must be a relative path inside the game's directory");
            files[i].clear();
        }
    }
    files.erase(std::remove(files.begin(), files.end(), std::string()), files.end());
    return files;
}

/// The optional "input": the path of the file that binds keys to actions.
void read_input_file(Checker& checker, const json::Value& root, Manifest& manifest) {
    std::string path;
    if (!checker.text(root, "input", "", false, path) || path.empty()) {
        return;
    }
    if (!is_safe_relative(path)) {
        checker.error("input",
                      "\"" + path + "\" must be a relative path inside the game's directory");
        return;
    }
    manifest.input_file = path;
}

void read_input_map(const Context& context, const std::string& manifest_path, Game& game) {
    if (game.manifest.input_file.empty()) {
        return;
    }
    const std::string path = context.directory + game.manifest.input_file;
    const std::optional<std::string> text = (*context.read)(path);
    if (!text) {
        context.diagnostics->push_back(
            Diagnostic{manifest_path, "input", "cannot read the file \"" + path + "\""});
        return;
    }
    platform::InputMapResult result = platform::parse_input_map(*text, path);
    for (platform::InputProblem& problem : result.problems) {
        context.diagnostics->push_back(Diagnostic{std::move(problem.file), std::move(problem.path),
                                                  std::move(problem.message)});
    }
    game.input = std::move(result.map);
}

bool read_manifest(const Context& context, Checker& checker, const json::Value& root,
                   Manifest& manifest) {
    const json::Value* object = checker.object(root, "");
    if (object == nullptr) {
        return false;
    }
    checker.reject_unknown(*object, "",
                           {"$schema", "schema_version", "name", "description", "modules",
                            "simulation", "params", "prefabs", "scenes", "scenarios", "input",
                            "start_scene"});
    if (const json::Value* version = object->find("schema_version"); version == nullptr) {
        checker.error("", "missing required key \"schema_version\"");
    } else if (!version->is_int() || version->as_int() != 1) {
        checker.error("schema_version", "only schema version 1 is supported");
    }
    static_cast<void>(checker.name(*object, "name", "", true, manifest.name));
    static_cast<void>(checker.text(*object, "description", "", false, manifest.description));
    read_modules(context, checker, *object, manifest);
    read_simulation(checker, *object, manifest);
    read_params(context, checker, *object, manifest);
    manifest.prefab_files = read_file_list(checker, *object, "prefabs");
    manifest.scene_files = read_file_list(checker, *object, "scenes");
    manifest.scenario_files = read_file_list(checker, *object, "scenarios");
    read_input_file(checker, *object, manifest);
    static_cast<void>(checker.name(*object, "start_scene", "", true, manifest.start_scene));
    return true;
}

// ---- prefabs and scenes ----------------------------------------------------

std::optional<PrefabData> read_prefab(const Context& context, const std::string& file,
                                      const json::Value& root) {
    Checker checker(file, *context.diagnostics);
    const json::Value* object = checker.object(root, "");
    if (object == nullptr) {
        return std::nullopt;
    }
    checker.reject_unknown(*object, "",
                           {"$schema", "schema_version", "name", "extends", "components"});
    if (const json::Value* version = object->find("schema_version"); version == nullptr) {
        checker.error("", "missing required key \"schema_version\"");
    } else if (!version->is_int() || version->as_int() != 1) {
        checker.error("schema_version", "only schema version 1 is supported");
    }
    PrefabData prefab;
    static_cast<void>(checker.name(*object, "name", "", true, prefab.name));
    static_cast<void>(checker.name(*object, "extends", "", false, prefab.extends));
    if (const json::Value* components = object->find("components"); components != nullptr) {
        prefab.components = read_components(context, checker, *components, "components");
    }
    return prefab;
}

void read_entity(const Context& context, Checker& checker, const json::Value& value,
                 const std::string& path, SceneData& scene) {
    const json::Value* object = checker.object(value, path);
    if (object == nullptr) {
        return;
    }
    checker.reject_unknown(*object, path, {"name", "prefab", "components"});
    EntityData entity;
    if (checker.name(*object, "name", path, false, entity.name)) {
        const bool duplicate =
            std::any_of(scene.entities.begin(), scene.entities.end(),
                        [&entity](const EntityData& other) { return other.name == entity.name; });
        if (duplicate) {
            checker.error(join_path(path, "name"),
                          "two entities of the scene are named \"" + entity.name + "\"");
        }
    }
    static_cast<void>(checker.name(*object, "prefab", path, false, entity.prefab));
    if (const json::Value* components = object->find("components"); components != nullptr) {
        entity.components =
            read_components(context, checker, *components, join_path(path, "components"));
    }
    if (entity.prefab.empty() && object->find("components") == nullptr) {
        checker.error(path, R"(an entity needs a "prefab", "components", or both)");
    }
    scene.entities.push_back(std::move(entity));
}

std::optional<SceneData> read_scene(const Context& context, const std::string& file,
                                    const json::Value& root) {
    Checker checker(file, *context.diagnostics);
    const json::Value* object = checker.object(root, "");
    if (object == nullptr) {
        return std::nullopt;
    }
    checker.reject_unknown(*object, "", {"$schema", "schema_version", "name", "entities"});
    if (const json::Value* version = object->find("schema_version"); version == nullptr) {
        checker.error("", "missing required key \"schema_version\"");
    } else if (!version->is_int() || version->as_int() != 1) {
        checker.error("schema_version", "only schema version 1 is supported");
    }
    SceneData scene;
    static_cast<void>(checker.name(*object, "name", "", true, scene.name));
    if (const json::Value* entities = checker.array(*object, "entities", ""); entities != nullptr) {
        for (std::size_t i = 0; i < entities->items().size(); ++i) {
            read_entity(context, checker, entities->items()[i], index_path("entities", i), scene);
        }
    } else if (object->find("entities") == nullptr) {
        checker.error("", "missing required key \"entities\"");
    }
    return scene;
}

/// Unique prefab names, existing `extends` targets, and no loops in the chains.
void check_prefab_links(const Context& context, const Game& game,
                        const std::vector<std::string>& prefab_files) {
    const std::vector<std::string> prefab_names = names_of(game.prefabs);
    std::set<std::string> seen;
    for (std::size_t i = 0; i < game.prefabs.size(); ++i) {
        Checker checker(prefab_files[i], *context.diagnostics);
        const PrefabData& prefab = game.prefabs[i];
        if (!seen.insert(prefab.name).second) {
            checker.error("name", "another prefab is already named \"" + prefab.name + "\"");
        }
        if (prefab.extends.empty()) {
            continue;
        }
        if (game.find_prefab(prefab.extends) == nullptr) {
            checker.error("extends", "unknown prefab \"" + prefab.extends + "\"" +
                                         suggestion(prefab.extends, prefab_names));
            continue;
        }
        // Follow the chain; more steps than prefabs means it loops.
        const PrefabData* step = &prefab;
        for (std::size_t hops = 0; step != nullptr && !step->extends.empty(); ++hops) {
            if (hops > game.prefabs.size()) {
                checker.error("extends", "the prefab chain of \"" + prefab.name + "\" loops");
                break;
            }
            step = game.find_prefab(step->extends);
        }
    }
}

/// Unique scene names and existing prefabs for every entity.
void check_scene_links(const Context& context, const Game& game,
                       const std::vector<std::string>& scene_files) {
    const std::vector<std::string> prefab_names = names_of(game.prefabs);
    std::set<std::string> seen;
    for (std::size_t i = 0; i < game.scenes.size(); ++i) {
        Checker checker(scene_files[i], *context.diagnostics);
        const SceneData& scene = game.scenes[i];
        if (!seen.insert(scene.name).second) {
            checker.error("name", "another scene is already named \"" + scene.name + "\"");
        }
        for (std::size_t e = 0; e < scene.entities.size(); ++e) {
            const std::string& prefab = scene.entities[e].prefab;
            if (!prefab.empty() && game.find_prefab(prefab) == nullptr) {
                checker.error(join_path(index_path("entities", e), "prefab"),
                              "unknown prefab \"" + prefab + "\"" +
                                  suggestion(prefab, prefab_names));
            }
        }
    }
}

/// Checks what spans files: unique names, existing prefabs, no prefab cycles,
/// an existing start scene.
void check_links(const Context& context, const Game& game,
                 const std::vector<std::string>& prefab_files,
                 const std::vector<std::string>& scene_files, const std::string& manifest_path) {
    check_prefab_links(context, game, prefab_files);
    check_scene_links(context, game, scene_files);
    if (!game.manifest.start_scene.empty() &&
        game.find_scene(game.manifest.start_scene) == nullptr) {
        Checker checker(manifest_path, *context.diagnostics);
        checker.error("start_scene",
                      "no scene is named \"" + game.manifest.start_scene + "\"" +
                          suggestion(game.manifest.start_scene, names_of(game.scenes)));
    }
}

} // namespace

const PrefabData* Game::find_prefab(std::string_view name) const {
    const auto found =
        std::find_if(prefabs.begin(), prefabs.end(),
                     [name](const PrefabData& prefab) { return prefab.name == name; });
    return found == prefabs.end() ? nullptr : &*found;
}

const SceneData* Game::find_scene(std::string_view name) const {
    const auto found = std::find_if(scenes.begin(), scenes.end(),
                                    [name](const SceneData& scene) { return scene.name == name; });
    return found == scenes.end() ? nullptr : &*found;
}

const ScenarioData* Game::find_scenario(std::string_view name) const {
    const auto found =
        std::find_if(scenarios.begin(), scenarios.end(),
                     [name](const ScenarioData& scenario) { return scenario.name == name; });
    return found == scenarios.end() ? nullptr : &*found;
}

LoadedGame load_game(const std::string& manifest_path, const FileReader& read,
                     const Catalog& catalog) {
    LoadedGame loaded;
    Context context{&read, &catalog, &loaded.diagnostics, directory_of(manifest_path), {}};

    const std::optional<json::Value> root = read_json(context, manifest_path, manifest_path, "");
    if (!root) {
        return loaded;
    }
    Checker manifest_checker(manifest_path, loaded.diagnostics);
    if (!read_manifest(context, manifest_checker, *root, loaded.game.manifest)) {
        return loaded;
    }
    context.modules = loaded.game.manifest.modules;

    std::vector<std::string> prefab_files;
    for (std::size_t i = 0; i < loaded.game.manifest.prefab_files.size(); ++i) {
        const std::string path = context.directory + loaded.game.manifest.prefab_files[i];
        const std::optional<json::Value> data =
            read_json(context, path, manifest_path, index_path("prefabs", i));
        if (!data) {
            continue;
        }
        if (std::optional<PrefabData> prefab = read_prefab(context, path, *data)) {
            loaded.game.prefabs.push_back(std::move(*prefab));
            prefab_files.push_back(path);
        }
    }
    std::vector<std::string> scene_files;
    for (std::size_t i = 0; i < loaded.game.manifest.scene_files.size(); ++i) {
        const std::string path = context.directory + loaded.game.manifest.scene_files[i];
        const std::optional<json::Value> data =
            read_json(context, path, manifest_path, index_path("scenes", i));
        if (!data) {
            continue;
        }
        if (std::optional<SceneData> scene = read_scene(context, path, *data)) {
            loaded.game.scenes.push_back(std::move(*scene));
            scene_files.push_back(path);
        }
    }
    check_links(context, loaded.game, prefab_files, scene_files, manifest_path);

    std::vector<std::string> scenario_files;
    for (std::size_t i = 0; i < loaded.game.manifest.scenario_files.size(); ++i) {
        const std::string path = context.directory + loaded.game.manifest.scenario_files[i];
        const std::optional<json::Value> data =
            read_json(context, path, manifest_path, index_path("scenarios", i));
        if (!data) {
            continue;
        }
        if (std::optional<ScenarioData> scenario = read_scenario(context, path, *data)) {
            loaded.game.scenarios.push_back(std::move(*scenario));
            scenario_files.push_back(path);
        }
    }
    check_scenario_links(context, loaded.game, scenario_files);
    read_input_map(context, manifest_path, loaded.game);
    return loaded;
}

std::vector<ComponentData> resolve_components(const Game& game, std::string_view prefab,
                                              const std::vector<ComponentData>& own) {
    // The chain, leaf first, then reversed so the base comes first.
    std::vector<const PrefabData*> chain;
    for (const PrefabData* step = game.find_prefab(prefab);
         step != nullptr && chain.size() <= game.prefabs.size();
         step = game.find_prefab(step->extends)) {
        chain.push_back(step);
    }
    std::reverse(chain.begin(), chain.end());

    std::vector<ComponentData> result;
    const auto merge = [&result](const ComponentData& layer) {
        const auto existing =
            std::find_if(result.begin(), result.end(),
                         [&layer](const ComponentData& item) { return item.name == layer.name; });
        if (existing == result.end()) {
            result.push_back(layer);
            return;
        }
        for (const auto& member : layer.fields.members()) {
            existing->fields.set(member.first, member.second);
        }
    };
    for (const PrefabData* step : chain) {
        for (const ComponentData& component : step->components) {
            merge(component);
        }
    }
    for (const ComponentData& component : own) {
        merge(component);
    }
    return result;
}

} // namespace ge::game
