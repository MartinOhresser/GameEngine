#include "ge/game/game.hpp"

#include "check.hpp"
#include "scenario_read.hpp"

#include <algorithm>
#include <set>

namespace ge::game {

namespace {

using namespace detail;

/// Most names a set field can hold: one bit of its u32 per name.
constexpr std::size_t max_set_size = 32;

/// One name given for a ref field: it must be in the game's list, and in a set
/// it must be among the first 32 names, which are the ones a bit mask can hold.
void check_ref_name(Checker& checker, const std::string& path, const FieldDef& field,
                    const std::vector<std::string>& names, const std::string& name) {
    if (names.empty()) {
        checker.error(path, "this field names an item of the game's " + field.ref +
                                " list, but the game declares none (see the manifest's " +
                                (field.ref == "atlas" ? "atlases" : "lists") + ")");
        return;
    }
    const auto found = std::find(names.begin(), names.end(), name);
    if (found == names.end()) {
        checker.error(path, "no " + field.ref + " is named \"" + name + "\"" +
                                suggestion(name, names) + " (expected one of: " + join(names) +
                                ")");
    } else if (field.ref_set && static_cast<std::size_t>(found - names.begin()) >= max_set_size) {
        checker.error(path, "\"" + name + "\" is past the first " + std::to_string(max_set_size) +
                                " " + field.ref + " names: a set holds no more");
    }
}

/// A u32 field that refers to a list of the game (an atlas, say) may name the
/// item instead of giving its index; a number must be a valid index. A set
/// field takes an array of names, or a single name, or a bit mask.
void check_ref_value(const Context& context, Checker& checker, const std::string& path,
                     const FieldDef& field, const json::Value& value) {
    const auto table = context.refs.find(field.ref);
    const std::vector<std::string> none;
    const std::vector<std::string>& names = table == context.refs.end() ? none : table->second;
    if (value.is_string()) {
        check_ref_name(checker, path, field, names, value.as_string());
        return;
    }
    if (field.ref_set && value.is_array()) {
        for (std::size_t i = 0; i < value.items().size(); ++i) {
            const json::Value& item = value.items()[i];
            if (item.is_string()) {
                check_ref_name(checker, index_path(path, i), field, names, item.as_string());
            } else {
                checker.error(index_path(path, i),
                              "expected a " + field.ref + " name, found " + describe(item));
            }
        }
        return;
    }
    check_value(checker, path, field.type, value);
    if (!field.ref_set && value.is_int() && !names.empty() &&
        value.as_int() >= static_cast<std::int64_t>(names.size())) {
        checker.error(path, "there is no " + field.ref + " number " +
                                std::to_string(value.as_int()) + " (the game has " +
                                std::to_string(names.size()) + ": " + join(names) + ")");
    }
}

void read_component_fields(const Context& context, Checker& checker, const json::Value& value,
                           const std::string& path, const ComponentDef& component,
                           ComponentData& out) {
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
        if (field->ref.empty()) {
            check_value(checker, field_path, field->type, member.second);
        } else {
            check_ref_value(context, checker, field_path, *field, member.second);
        }
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
        read_component_fields(context, checker, member.second, component_path, *component, data);
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

/// The optional "lists": named lists of names that components refer to
/// ("ref" and "ref_set" fields), such as item kinds, categories or actions.
void read_lists(Checker& checker, const json::Value& root, Manifest& manifest) {
    const json::Value* lists = root.find("lists");
    if (lists == nullptr || checker.object(*lists, "lists") == nullptr) {
        return;
    }
    for (const auto& member : lists->members()) {
        const std::string path = join_path("lists", member.first);
        if (!is_snake(member.first)) {
            checker.error(path, "a list name must be lower_snake_case");
        } else if (member.first == "atlas") {
            checker.error(path, "the atlas list is made by the manifest's \"atlases\" files");
        }
        std::vector<std::string> names;
        if (!member.second.is_array()) {
            checker.error(path, "expected an array of names, found " + describe(member.second));
            continue;
        }
        for (std::size_t i = 0; i < member.second.items().size(); ++i) {
            const json::Value& item = member.second.items()[i];
            const std::string item_path = index_path(path, i);
            if (!item.is_string() || !is_snake(item.as_string())) {
                checker.error(item_path, "a name must be a lower_snake_case string");
            } else if (std::find(names.begin(), names.end(), item.as_string()) != names.end()) {
                checker.error(item_path, "\"" + item.as_string() + "\" is already in this list");
            } else {
                names.push_back(item.as_string());
            }
        }
        manifest.lists.emplace_back(member.first, std::move(names));
    }
}

bool read_manifest(const Context& context, Checker& checker, const json::Value& root,
                   Manifest& manifest) {
    const json::Value* object = checker.object(root, "");
    if (object == nullptr) {
        return false;
    }
    checker.reject_unknown(*object, "",
                           {"$schema", "schema_version", "name", "description", "modules",
                            "simulation", "params", "lists", "atlases", "prefabs", "scenes",
                            "scenarios", "input", "start_scene"});
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
    read_lists(checker, *object, manifest);
    manifest.atlas_files = read_file_list(checker, *object, "atlases");
    manifest.prefab_files = read_file_list(checker, *object, "prefabs");
    manifest.scene_files = read_file_list(checker, *object, "scenes");
    manifest.scenario_files = read_file_list(checker, *object, "scenarios");
    read_input_file(checker, *object, manifest);
    static_cast<void>(checker.name(*object, "start_scene", "", true, manifest.start_scene));
    return true;
}

// ---- prefabs and scenes ----------------------------------------------------

/// Largest cell of an atlas: well beyond any sprite, small enough to catch a typo.
constexpr std::int64_t max_cell_size = 4096;

bool read_cell_size(Checker& checker, const json::Value& object, const char* key, int& out) {
    const json::Value* value = object.find(key);
    if (value == nullptr) {
        checker.error("", std::string("missing required key \"") + key + "\"");
        return false;
    }
    if (!value->is_int() || value->as_int() < 1 || value->as_int() > max_cell_size) {
        checker.error(key, "must be a whole number from 1 to " + std::to_string(max_cell_size));
        return false;
    }
    out = static_cast<int>(value->as_int());
    return true;
}

std::optional<AtlasData> read_atlas(const Context& context, const std::string& file,
                                    const json::Value& root) {
    Checker checker(file, *context.diagnostics);
    const json::Value* object = checker.object(root, "");
    if (object == nullptr) {
        return std::nullopt;
    }
    checker.reject_unknown(
        *object, "", {"$schema", "schema_version", "name", "image", "cell_width", "cell_height"});
    if (const json::Value* version = object->find("schema_version"); version == nullptr) {
        checker.error("", "missing required key \"schema_version\"");
    } else if (!version->is_int() || version->as_int() != 1) {
        checker.error("schema_version", "only schema version 1 is supported");
    }
    AtlasData atlas;
    static_cast<void>(checker.name(*object, "name", "", true, atlas.name));
    std::string image;
    if (checker.text(*object, "image", "", true, image)) {
        if (!is_safe_relative(image)) {
            checker.error("image",
                          "\"" + image + "\" must be a relative path next to the atlas file");
        } else {
            atlas.image = directory_of(file) + image;
            if (!(*context.read)(atlas.image)) {
                checker.error("image", "cannot read the image \"" + atlas.image + "\"");
            }
        }
    }
    static_cast<void>(read_cell_size(checker, *object, "cell_width", atlas.cell_width));
    static_cast<void>(read_cell_size(checker, *object, "cell_height", atlas.cell_height));
    return atlas;
}

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

std::vector<std::string> Game::ref_names(std::string_view ref) const {
    std::vector<std::string> names;
    if (ref == "atlas") {
        for (const AtlasData& atlas : atlases) {
            names.push_back(atlas.name);
        }
        return names;
    }
    for (const auto& list : manifest.lists) {
        if (list.first == ref) {
            return list.second;
        }
    }
    return names;
}

LoadedGame load_game(const std::string& manifest_path, const FileReader& read,
                     const Catalog& catalog) {
    LoadedGame loaded;
    Context context{&read, &catalog, &loaded.diagnostics, directory_of(manifest_path), {}, {}};

    const std::optional<json::Value> root = read_json(context, manifest_path, manifest_path, "");
    if (!root) {
        return loaded;
    }
    Checker manifest_checker(manifest_path, loaded.diagnostics);
    if (!read_manifest(context, manifest_checker, *root, loaded.game.manifest)) {
        return loaded;
    }
    context.modules = loaded.game.manifest.modules;

    for (std::size_t i = 0; i < loaded.game.manifest.atlas_files.size(); ++i) {
        const std::string path = context.directory + loaded.game.manifest.atlas_files[i];
        const std::optional<json::Value> data =
            read_json(context, path, manifest_path, index_path("atlases", i));
        if (!data) {
            continue;
        }
        std::optional<AtlasData> atlas = read_atlas(context, path, *data);
        if (!atlas) {
            continue;
        }
        const bool duplicate =
            std::any_of(loaded.game.atlases.begin(), loaded.game.atlases.end(),
                        [&atlas](const AtlasData& other) { return other.name == atlas->name; });
        if (duplicate) {
            context.diagnostics->push_back(
                Diagnostic{path, "name", "two atlases are named \"" + atlas->name + "\""});
        }
        loaded.game.atlases.push_back(std::move(*atlas));
    }
    context.refs["atlas"] = loaded.game.ref_names("atlas");
    for (const auto& list : loaded.game.manifest.lists) {
        context.refs[list.first] = list.second;
    }

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
