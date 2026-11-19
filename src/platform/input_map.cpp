#include "ge/platform/input_map.hpp"

#include "ge/json/json.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <utility>

namespace ge::platform {

namespace {

constexpr std::array<std::string_view, 4> source_kinds = {"key", "mouse", "pad", "touch"};

bool is_name_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

bool is_snake(std::string_view text) {
    return !text.empty() && text.front() >= 'a' && text.front() <= 'z' &&
           std::all_of(text.begin(), text.end(), is_name_char);
}

float clamp_axis(float value) {
    return std::clamp(value, -1.0F, 1.0F);
}

/// The pointer's position is in pixels, not an axis: it must not be clamped.
/// "mouse:x", "mouse:y", and the position of a finger: "touch:x", "touch:y2"...
bool is_pointer_position(std::string_view source) {
    if (source == "mouse:x" || source == "mouse:y") {
        return true;
    }
    constexpr std::string_view touch = "touch:";
    if (!source.starts_with(touch) || source.size() <= touch.size()) {
        return false;
    }
    const std::string_view name = source.substr(touch.size());
    return (name.front() == 'x' || name.front() == 'y') &&
           std::all_of(name.begin() + 1, name.end(), [](char c) { return c >= '0' && c <= '9'; });
}

class Reader {
public:
    Reader(std::string_view file, std::vector<InputProblem>* problems)
        : file_(file), problems_(problems) {}

    void problem(const std::string& path, const std::string& message) {
        problems_->push_back(
            InputProblem{std::string(file_), path.empty() ? "<root>" : path, message});
    }

    void read_root(const json::Value& root, InputMap& map) {
        if (!root.is_object()) {
            problem("", "the file must contain an object");
            return;
        }
        for (const auto& [key, value] : root.members()) {
            if (key != "schema_version" && key != "bindings" && key != "$schema" &&
                key != "description") {
                problem(key, "unknown key \"" + key +
                                 "\"; expected schema_version, bindings, description");
            }
            (void)value;
        }
        const json::Value* version = root.find("schema_version");
        if (version == nullptr || !version->is_int() || version->as_int() != 1) {
            problem("schema_version", "must be the integer 1");
        }
        const json::Value* bindings = root.find("bindings");
        if (bindings == nullptr || !bindings->is_array()) {
            problem("bindings", "must be an array of {action, sources}");
            return;
        }
        for (std::size_t i = 0; i < bindings->items().size(); ++i) {
            read_binding("bindings[" + std::to_string(i) + "]", bindings->items()[i], map);
        }
    }

private:
    void read_binding(const std::string& path, const json::Value& entry, InputMap& map) {
        if (!entry.is_object()) {
            problem(path, "must be an object {action, sources}");
            return;
        }
        for (const auto& member : entry.members()) {
            if (member.first != "action" && member.first != "sources") {
                problem(path + "." + member.first,
                        "unknown key \"" + member.first + "\"; expected action, sources");
            }
        }
        const json::Value* action = entry.find("action");
        if (action == nullptr || !action->is_string() || !is_snake(action->as_string())) {
            problem(path + ".action", "must be a lower_snake_case name such as \"move_x\"");
            return;
        }
        if (!actions_.insert(action->as_string()).second) {
            problem(path + ".action", "the action \"" + action->as_string() +
                                          "\" is listed twice; put all its sources in one entry");
        }
        const json::Value* sources = entry.find("sources");
        if (sources == nullptr || !sources->is_object() || sources->members().empty()) {
            problem(path + ".sources",
                    "must be an object of source: scale, e.g. { \"key:space\": 1 }");
            return;
        }
        for (const auto& [source, scale] : sources->members()) {
            std::string where = path;
            where += ".sources.";
            where += source;
            if (!is_valid_source(source)) {
                problem(where, "\"" + source +
                                   "\" is not a source; write kind:name in lower case, with a "
                                   "kind among key, mouse, pad, touch (e.g. \"key:left\")");
            } else if (!scale.is_number()) {
                problem(where, "the scale must be a number (1 or -1 for a button)");
            } else {
                map.bind(action->as_string(), source, static_cast<float>(scale.as_real()));
            }
        }
    }

    std::string_view file_;
    std::vector<InputProblem>* problems_;
    std::set<std::string> actions_;
};

} // namespace

void RawState::set(std::string_view source, float value) {
    const auto found = values_.find(source);
    if (found == values_.end()) {
        values_.emplace(std::string(source), value);
    } else {
        found->second = value;
    }
}

float RawState::value(std::string_view source) const {
    const auto found = values_.find(source);
    return found == values_.end() ? 0.0F : found->second;
}

void InputMap::bind(std::string action, std::string source, float scale) {
    bindings_.push_back(Binding{std::move(action), std::move(source), scale});
}

void InputMap::apply(const RawState& raw, app::LiveInput& live) const {
    // Bindings of one action are written one after the other in the file, but
    // nothing relies on it: sum per action first, then set once.
    std::map<std::string, float, std::less<>> sums;
    std::set<std::string_view> positions;
    std::vector<const std::string*> order;
    for (const Binding& binding : bindings_) {
        const auto [slot, inserted] = sums.try_emplace(binding.action, 0.0F);
        if (inserted) {
            order.push_back(&slot->first);
        }
        slot->second += binding.scale * raw.value(binding.source);
        if (is_pointer_position(binding.source)) {
            positions.insert(binding.action);
        }
    }
    for (const std::string* action : order) {
        const float sum = sums.find(*action)->second;
        live.set(*action, positions.contains(*action) ? sum : clamp_axis(sum));
    }
}

bool is_valid_source(std::string_view source) {
    const std::size_t colon = source.find(':');
    if (colon == std::string_view::npos) {
        return false;
    }
    const std::string_view kind = source.substr(0, colon);
    const std::string_view name = source.substr(colon + 1);
    return std::find(source_kinds.begin(), source_kinds.end(), kind) != source_kinds.end() &&
           !name.empty() && std::all_of(name.begin(), name.end(), is_name_char);
}

InputMapResult parse_input_map(std::string_view text, std::string_view file) {
    InputMapResult result;
    const json::ParseResult parsed = json::parse(text, json::relaxed);
    if (parsed.error.has_value()) {
        result.problems.push_back(
            InputProblem{std::string(file), "", "invalid JSON at " + parsed.error->to_string()});
        return result;
    }
    Reader reader(file, &result.problems);
    reader.read_root(parsed.value, result.map);
    return result;
}

} // namespace ge::platform
