#include "ge/game/scenario.hpp"

#include "scenario_read.hpp"

#include <cstdlib>
#include <set>

namespace ge::game {

namespace {

using namespace detail;

constexpr std::uint64_t max_ticks = 10'000'000;

std::vector<std::string> split_words(const std::string& text) {
    std::vector<std::string> words;
    std::string word;
    for (const char c : text) {
        if (c == ' ' || c == '\t') {
            if (!word.empty()) {
                words.push_back(std::move(word));
                word.clear();
            }
        } else {
            word += c;
        }
    }
    if (!word.empty()) {
        words.push_back(std::move(word));
    }
    return words;
}

std::optional<Compare> compare_from(const std::string& word) {
    if (word == "==") {
        return Compare::Eq;
    }
    if (word == "!=") {
        return Compare::Ne;
    }
    if (word == "<") {
        return Compare::Lt;
    }
    if (word == "<=") {
        return Compare::Le;
    }
    if (word == ">") {
        return Compare::Gt;
    }
    if (word == ">=") {
        return Compare::Ge;
    }
    return std::nullopt;
}

std::vector<std::string> split_dots(const std::string& text) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('.', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        parts.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

/// Hexadecimal digits only, 1 to 16 of them.
bool parse_hex(const std::string& text, std::uint64_t& out) {
    if (text.empty() || text.size() > 16) {
        return false;
    }
    out = 0;
    for (const char c : text) {
        std::uint64_t digit = 0;
        if (c >= '0' && c <= '9') {
            digit = static_cast<std::uint64_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = static_cast<std::uint64_t>(c - 'a') + 10U;
        } else {
            return false;
        }
        out = (out << 4U) | digit;
    }
    return true;
}

constexpr const char* check_forms =
    "expected \"entity.module.Component.field > number\", \"entities == number\", "
    "\"entity alive\", \"entity gone\", \"scene is name\", \"game quits\", "
    "\"host calls command\", \"list name selected == number\" or \"fingerprint == hex\"";

/// "list games selected == 1".
std::string parse_list_selected(const std::vector<std::string>& words, Check& out) {
    const std::optional<Compare> compare = compare_from(words[3]);
    if (!compare) {
        return "unknown comparison \"" + words[3] + "\" (use ==, !=, <, <=, > or >=)";
    }
    const json::ParseResult value = json::parse(words[4]);
    if (!value.ok() || !value.value.is_int()) {
        return "\"" + words[4] + "\" is not a whole number";
    }
    out.kind = CheckKind::ListSelected;
    out.entity = words[1];
    out.compare = *compare;
    out.value = value.value;
    return {};
}

std::string parse_comparison(const std::vector<std::string>& words, Check& out) {
    const std::optional<Compare> compare = compare_from(words[1]);
    if (!compare) {
        return "unknown comparison \"" + words[1] + "\" (use ==, !=, <, <=, > or >=)";
    }
    out.compare = *compare;
    if (words[0] == "fingerprint") {
        out.kind = CheckKind::Fingerprint;
        if ((*compare != Compare::Eq && *compare != Compare::Ne) ||
            !parse_hex(words[2], out.fingerprint)) {
            return "a fingerprint check is \"fingerprint == \" or \"!=\" followed by up to 16 "
                   "lowercase hexadecimal digits";
        }
        return {};
    }
    const json::ParseResult value = json::parse(words[2]);
    if (!value.ok() || !(value.value.is_number() || value.value.is_bool())) {
        return "\"" + words[2] + "\" is not a number or a boolean";
    }
    out.value = value.value;
    if (words[0] == "entities") {
        out.kind = CheckKind::EntityCount;
        return value.value.is_int() ? std::string() : "the entity count is compared to an integer";
    }
    const std::vector<std::string> parts = split_dots(words[0]);
    if (parts.size() != 4 || parts[0].empty() || parts[1].empty() || parts[2].empty() ||
        parts[3].empty()) {
        return '"' + words[0] + R"(" is not "entity.module.Component.field")";
    }
    out.kind = CheckKind::Field;
    out.entity = parts[0];
    out.component = parts[1] + "." + parts[2];
    out.field = parts[3];
    return {};
}

} // namespace

std::string format_fingerprint(std::uint64_t fingerprint) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string text(16, '0');
    for (std::size_t i = 0; i < text.size(); ++i) {
        text[text.size() - 1 - i] = digits[(fingerprint >> (4U * i)) & 0xFU];
    }
    return text;
}

std::string parse_check(const std::string& text, Check& out) {
    const std::vector<std::string> words = split_words(text);
    out.text = text;
    if (words.size() == 2 && (words[1] == "alive" || words[1] == "gone")) {
        out.kind = words[1] == "alive" ? CheckKind::Alive : CheckKind::Gone;
        out.entity = words[0];
        return {};
    }
    if (words.size() == 2 && words[0] == "game" && words[1] == "quits") {
        out.kind = CheckKind::Quit;
        return {};
    }
    if (words.size() == 5 && words[0] == "list" && words[2] == "selected") {
        return parse_list_selected(words, out);
    }
    if (words.size() == 3 && words[0] == "host" && words[1] == "calls") {
        out.kind = CheckKind::HostCall;
        out.entity = words[2];
        return {};
    }
    if (words.size() == 3 && words[0] == "scene" && words[1] == "is") {
        out.kind = CheckKind::Scene;
        out.entity = words[2];
        return {};
    }
    if (words.size() == 3) {
        return parse_comparison(words, out);
    }
    return check_forms;
}

} // namespace ge::game

namespace ge::game::detail {

namespace {

void read_inputs(Checker& checker, const json::Value& root, ScenarioData& scenario) {
    const json::Value* inputs = checker.array(root, "inputs", "");
    if (inputs == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < inputs->items().size(); ++i) {
        const std::string path = index_path("inputs", i);
        const json::Value* object = checker.object(inputs->items()[i], path);
        if (object == nullptr) {
            continue;
        }
        checker.reject_unknown(*object, path, {"tick", "action", "value"});
        ScenarioInput input;
        const json::Value* tick = object->find("tick");
        if (tick == nullptr || !tick->is_int() || tick->as_int() < 0) {
            checker.error(join_path(path, "tick"),
                          "expected a whole number of ticks, zero or more");
        } else {
            input.tick = static_cast<std::uint64_t>(tick->as_int());
        }
        static_cast<void>(checker.name(*object, "action", path, true, input.action));
        if (const json::Value* value = object->find("value"); value != nullptr) {
            if (value->is_number()) {
                input.value = static_cast<float>(value->as_real());
            } else {
                checker.error(join_path(path, "value"),
                              "expected a number, found " + describe(*value));
            }
        }
        if (scenario.ticks > 0 && input.tick >= scenario.ticks) {
            checker.error(join_path(path, "tick"), "the scenario ends at tick " +
                                                       std::to_string(scenario.ticks) +
                                                       ", before this input");
        }
        scenario.inputs.push_back(std::move(input));
    }
}

/// The optional "lists": run-time lists a host would fill, as `name: [texts]`.
void read_lists(Checker& checker, const json::Value& root, ScenarioData& scenario) {
    const json::Value* lists = root.find("lists");
    if (lists == nullptr || checker.object(*lists, "lists") == nullptr) {
        return;
    }
    for (const auto& member : lists->members()) {
        const std::string path = join_path("lists", member.first);
        if (!member.second.is_array()) {
            checker.error(path, "expected an array of texts, found " + describe(member.second));
            continue;
        }
        std::vector<std::string> items;
        for (std::size_t i = 0; i < member.second.items().size(); ++i) {
            const json::Value& item = member.second.items()[i];
            if (item.is_string()) {
                items.push_back(item.as_string());
            } else {
                checker.error(index_path(path, i), "expected a text, found " + describe(item));
            }
        }
        scenario.lists.emplace_back(member.first, std::move(items));
    }
}

void read_check(Checker& checker, const json::Value& value, const std::string& path,
                ScenarioData& scenario) {
    Check check;
    std::string text;
    check.at = scenario.ticks;
    if (value.is_string()) {
        text = value.as_string();
    } else if (const json::Value* object = checker.object(value, path); object != nullptr) {
        checker.reject_unknown(*object, path, {"at", "check"});
        if (!checker.text(*object, "check", path, true, text)) {
            return;
        }
        if (const json::Value* at = object->find("at"); at != nullptr) {
            if (!at->is_int() || at->as_int() < 0 ||
                static_cast<std::uint64_t>(at->as_int()) > scenario.ticks) {
                checker.error(join_path(path, "at"),
                              "expected a tick between 0 and " + std::to_string(scenario.ticks));
                return;
            }
            check.at = static_cast<std::uint64_t>(at->as_int());
        }
    } else {
        return;
    }
    if (const std::string problem = parse_check(text, check); !problem.empty()) {
        checker.error(path, "cannot read the check \"" + text + "\": " + problem);
        return;
    }
    scenario.checks.push_back(std::move(check));
}

} // namespace

std::optional<ScenarioData> read_scenario(const Context& context, const std::string& file,
                                          const json::Value& root) {
    Checker checker(file, *context.diagnostics);
    const json::Value* object = checker.object(root, "");
    if (object == nullptr) {
        return std::nullopt;
    }
    checker.reject_unknown(*object, "",
                           {"$schema", "schema_version", "name", "description", "scene", "ticks",
                            "inputs", "lists", "expect"});
    if (const json::Value* version = object->find("schema_version"); version == nullptr) {
        checker.error("", "missing required key \"schema_version\"");
    } else if (!version->is_int() || version->as_int() != 1) {
        checker.error("schema_version", "only schema version 1 is supported");
    }
    ScenarioData scenario;
    static_cast<void>(checker.name(*object, "name", "", true, scenario.name));
    static_cast<void>(checker.text(*object, "description", "", false, scenario.description));
    static_cast<void>(checker.name(*object, "scene", "", false, scenario.scene));
    const json::Value* ticks = object->find("ticks");
    if (ticks == nullptr || !ticks->is_int() || ticks->as_int() < 1 ||
        static_cast<std::uint64_t>(ticks->as_int()) > max_ticks) {
        checker.error("ticks",
                      "expected a number of ticks between 1 and " + std::to_string(max_ticks));
    } else {
        scenario.ticks = static_cast<std::uint64_t>(ticks->as_int());
    }
    read_inputs(checker, *object, scenario);
    read_lists(checker, *object, scenario);
    if (const json::Value* expect = checker.array(*object, "expect", ""); expect != nullptr) {
        for (std::size_t i = 0; i < expect->items().size(); ++i) {
            read_check(checker, expect->items()[i], index_path("expect", i), scenario);
        }
    }
    return scenario;
}

namespace {

/// Does `value` make sense for a field of this type, and the comparison for the value?
std::string field_check_problem(const Check& check, module::FieldType type) {
    const bool boolean = type == module::FieldType::Bool;
    if (boolean != check.value.is_bool()) {
        return boolean ? "a boolean field is compared to true or false"
                       : "a numeric field is compared to a number";
    }
    if (boolean && check.compare != Compare::Eq && check.compare != Compare::Ne) {
        return "a boolean field can only be compared with == or !=";
    }
    return {};
}

void check_field(const Context& context, Checker& checker, const std::string& path,
                 const Check& check) {
    const ComponentDef* component = context.catalog->find_component(check.component);
    const std::string module_name = check.component.substr(0, check.component.find('.'));
    const bool listed = std::find(context.modules.begin(), context.modules.end(), module_name) !=
                        context.modules.end();
    if (component == nullptr || !listed) {
        checker.error(path, "unknown component \"" + check.component + "\" in the check \"" +
                                check.text + "\"");
        return;
    }
    const FieldDef* field = component->find_field(check.field);
    if (field == nullptr) {
        checker.error(path, "component \"" + check.component + "\" has no field \"" + check.field +
                                "\" (expected one of: " + join(names_of(component->fields)) + ")");
        return;
    }
    if (const std::string problem = field_check_problem(check, field->type); !problem.empty()) {
        checker.error(path, "in the check \"" + check.text + "\": " + problem);
    }
}

/// A name of the game's "runtime_list" list, as a scenario may use it.
void check_runtime_list(Checker& checker, const Game& game, const std::string& path,
                        const std::string& name) {
    const std::vector<std::string> known = game.ref_names("runtime_list");
    if (std::find(known.begin(), known.end(), name) == known.end()) {
        checker.error(path, "no run-time list is named \"" + name + "\" in the list " +
                                R"("runtime_list")" + suggestion(name, known));
    }
}

/// The names a check takes from the game itself: a scene, a host command.
void check_names_of_game(Checker& checker, const Game& game, const std::string& path,
                         const Check& check) {
    if (check.kind == CheckKind::Scene && game.find_scene(check.entity) == nullptr) {
        checker.error(path, "no scene is named \"" + check.entity + "\"" +
                                suggestion(check.entity, names_of(game.scenes)));
    }
    if (check.kind == CheckKind::ListSelected) {
        check_runtime_list(checker, game, path, check.entity);
    }
    if (check.kind != CheckKind::HostCall) {
        return;
    }
    const std::vector<std::string> commands = game.ref_names("host_command");
    if (std::find(commands.begin(), commands.end(), check.entity) == commands.end()) {
        checker.error(path, "no host command is named \"" + check.entity +
                                R"(" in the list "host_command")" +
                                suggestion(check.entity, commands));
    }
}

void check_one_scenario(const Context& context, const Game& game, const ScenarioData& scenario,
                        const std::string& file) {
    Checker checker(file, *context.diagnostics);
    const std::string& scene_name =
        scenario.scene.empty() ? game.manifest.start_scene : scenario.scene;
    const SceneData* start = game.find_scene(scene_name);
    if (start == nullptr && !scenario.scene.empty()) {
        checker.error("scene", "no scene is named \"" + scene_name + "\"" +
                                   suggestion(scene_name, names_of(game.scenes)));
    }
    for (const auto& list : scenario.lists) {
        check_runtime_list(checker, game, join_path("lists", list.first), list.first);
    }
    // The names a check may use: those of the scene it starts, then those of the other
    // scenes, which it can reach by changing scene while it runs.
    std::vector<std::string> named;
    std::vector<const SceneData*> scenes;
    if (start != nullptr) {
        scenes.push_back(start);
    }
    for (const SceneData& other : game.scenes) {
        if (&other != start) {
            scenes.push_back(&other);
        }
    }
    for (const SceneData* scene : scenes) {
        for (const EntityData& entity : scene->entities) {
            if (!entity.name.empty()) {
                named.push_back(entity.name);
            }
        }
    }
    for (std::size_t i = 0; i < scenario.checks.size(); ++i) {
        const Check& check = scenario.checks[i];
        const std::string path = index_path("expect", i);
        const bool needs_entity = check.kind == CheckKind::Field ||
                                  check.kind == CheckKind::Alive || check.kind == CheckKind::Gone;
        if (needs_entity && std::find(named.begin(), named.end(), check.entity) == named.end()) {
            checker.error(path, "the scene \"" + scene_name + "\" has no entity named \"" +
                                    check.entity + "\"" + suggestion(check.entity, named));
            continue;
        }
        check_names_of_game(checker, game, path, check);
        if (check.kind == CheckKind::Field) {
            check_field(context, checker, path, check);
        }
    }
}

} // namespace

void check_scenario_links(const Context& context, const Game& game,
                          const std::vector<std::string>& files) {
    std::set<std::string> seen;
    for (std::size_t i = 0; i < game.scenarios.size(); ++i) {
        const ScenarioData& scenario = game.scenarios[i];
        if (!seen.insert(scenario.name).second) {
            Checker checker(files[i], *context.diagnostics);
            checker.error("name", "another scenario is already named \"" + scenario.name + "\"");
        }
        check_one_scenario(context, game, scenario, files[i]);
    }
}

} // namespace ge::game::detail
