#pragma once

// Helpers shared by the files that read game data (game.cpp, scenario.cpp).
// Internal: not installed, not part of the public headers.

#include "ge/game/catalog.hpp"
#include "ge/game/game.hpp"
#include "ge/json/json.hpp"
#include "ge/module/spec.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ge::game::detail {

using module::Diagnostic;

inline bool is_lower(char c) {
    return c >= 'a' && c <= 'z';
}

inline bool is_snake(std::string_view text) {
    return !text.empty() && is_lower(text.front()) &&
           std::all_of(text.begin(), text.end(),
                       [](char c) { return is_lower(c) || (c >= '0' && c <= '9') || c == '_'; });
}

inline std::string describe(const json::Value& value) {
    switch (value.kind()) {
    case json::Value::Kind::Null:
        return "null";
    case json::Value::Kind::Bool:
        return "a boolean";
    case json::Value::Kind::Int:
        return "an integer";
    case json::Value::Kind::Real:
        return "a number";
    case json::Value::Kind::String:
        return "a string";
    case json::Value::Kind::Array:
        return "an array";
    case json::Value::Kind::Object:
        return "an object";
    }
    return "a value"; // GCOVR_EXCL_LINE: unreachable for valid enum values
}

inline std::string join(const std::vector<std::string>& words) {
    std::string result;
    for (const std::string& word : words) {
        if (!result.empty()) {
            result += ", ";
        }
        result += word;
    }
    return result;
}

inline std::size_t edit_distance(std::string_view a, std::string_view b) {
    std::vector<std::size_t> row(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) {
        row[j] = j;
    }
    for (std::size_t i = 1; i <= a.size(); ++i) {
        std::size_t diagonal = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t above = row[j];
            const std::size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + cost});
            diagonal = above;
        }
    }
    return row[b.size()];
}

/// " (did you mean "x"?)" when a candidate is close to `word`, else "".
inline std::string suggestion(std::string_view word, const std::vector<std::string>& candidates) {
    const std::string* best = nullptr;
    std::size_t best_distance = 3;
    for (const std::string& candidate : candidates) {
        const std::size_t distance = edit_distance(word, candidate);
        if (distance < best_distance) {
            best_distance = distance;
            best = &candidate;
        }
    }
    return best == nullptr ? "" : " (did you mean \"" + *best + "\"?)";
}

inline std::string join_path(const std::string& base, std::string_view key) {
    return base.empty() ? std::string(key) : base + "." + std::string(key);
}

inline std::string index_path(const std::string& base, std::size_t index) {
    return base + "[" + std::to_string(index) + "]";
}

/// Collects the problems found in one file.
class Checker {
public:
    Checker(std::string file, std::vector<Diagnostic>& out) : file_(std::move(file)), out_(&out) {}

    void error(const std::string& path, std::string message) {
        out_->push_back(Diagnostic{file_, path, std::move(message)});
    }

    const json::Value* object(const json::Value& value, const std::string& path) {
        if (!value.is_object()) {
            error(path, "expected an object, found " + describe(value));
            return nullptr;
        }
        return &value;
    }

    void reject_unknown(const json::Value& obj, const std::string& path,
                        const std::vector<std::string>& allowed) {
        for (const auto& member : obj.members()) {
            if (std::find(allowed.begin(), allowed.end(), member.first) == allowed.end()) {
                error(join_path(path, member.first),
                      "unknown key" + suggestion(member.first, allowed) +
                          " (expected one of: " + join(allowed) + ")");
            }
        }
    }

    bool text(const json::Value& obj, const char* key, const std::string& path, bool required,
              std::string& out) {
        const json::Value* value = obj.find(key);
        if (value == nullptr) {
            if (required) {
                error(path, std::string("missing required key \"") + key + "\"");
            }
            return false;
        }
        if (!value->is_string()) {
            error(join_path(path, key), "expected a string, found " + describe(*value));
            return false;
        }
        out = value->as_string();
        return true;
    }

    /// A required or optional snake_case name.
    bool name(const json::Value& obj, const char* key, const std::string& path, bool required,
              std::string& out) {
        if (!text(obj, key, path, required, out)) {
            return false;
        }
        if (!is_snake(out)) {
            error(join_path(path, key),
                  "\"" + out +
                      "\" must be lower_snake_case (lowercase letters, digits, underscores)");
            return false;
        }
        return true;
    }

    /// An optional array of strings.
    std::vector<std::string> texts(const json::Value& obj, const char* key,
                                   const std::string& path) {
        std::vector<std::string> result;
        const json::Value* value = obj.find(key);
        if (value == nullptr) {
            return result;
        }
        const std::string array_path = join_path(path, key);
        if (!value->is_array()) {
            error(array_path, "expected an array, found " + describe(*value));
            return result;
        }
        for (std::size_t i = 0; i < value->items().size(); ++i) {
            const json::Value& item = value->items()[i];
            if (item.is_string()) {
                result.push_back(item.as_string());
            } else {
                error(index_path(array_path, i), "expected a string, found " + describe(item));
            }
        }
        return result;
    }

    /// An optional array of arbitrary items; null when absent or not an array.
    const json::Value* array(const json::Value& obj, const char* key, const std::string& path) {
        const json::Value* value = obj.find(key);
        if (value == nullptr) {
            return nullptr;
        }
        if (!value->is_array()) {
            error(join_path(path, key), "expected an array, found " + describe(*value));
            return nullptr;
        }
        return value;
    }

private:
    std::string file_;
    std::vector<Diagnostic>* out_;
};

/// A relative path that stays inside the game's directory.
inline bool is_safe_relative(const std::string& path) {
    if (path.empty() || path.front() == '/' || path.find('\\') != std::string::npos ||
        path.find(':') != std::string::npos) {
        return false;
    }
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t end = path.find('/', start);
        if (end == std::string::npos) {
            end = path.size();
        }
        if (path.substr(start, end - start) == "..") {
            return false;
        }
        start = end + 1;
    }
    return true;
}

inline std::string directory_of(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

struct Context {
    const FileReader* read;
    const Catalog* catalog;
    std::vector<Diagnostic>* diagnostics;
    std::string directory;
    std::vector<std::string> modules; // the manifest's, for checking components
};

/// Reads and parses a JSON file with comments and trailing commas allowed.
inline std::optional<json::Value> read_json(const Context& context, const std::string& path,
                                            const std::string& referenced_from,
                                            const std::string& referenced_at) {
    const std::optional<std::string> text = (*context.read)(path);
    if (!text) {
        context.diagnostics->push_back(
            Diagnostic{referenced_from, referenced_at, "cannot read the file \"" + path + "\""});
        return std::nullopt;
    }
    json::ParseResult parsed = json::parse(*text, json::relaxed);
    if (parsed.error.has_value()) {
        context.diagnostics->push_back(
            Diagnostic{path, "", "invalid JSON at " + parsed.error->to_string()});
        return std::nullopt;
    }
    return std::move(parsed.value);
}

inline std::string type_name(module::FieldType type) {
    return std::string(module::to_string(type));
}

/// Checks one field value against its type, with a message that says what was found.
inline void check_value(Checker& checker, const std::string& path, module::FieldType type,
                        const json::Value& value) {
    if (module::value_fits(type, value)) {
        return;
    }
    const bool numeric = type != module::FieldType::Bool;
    if (numeric && value.is_number()) {
        checker.error(path, "the value does not fit the type " + type_name(type));
    } else {
        checker.error(path,
                      "expected a value of type " + type_name(type) + ", found " + describe(value));
    }
}

/// The names of `items`, in order, for "did you mean" and "expected one of".
template <typename T>
inline std::vector<std::string> names_of(const std::vector<T>& items) {
    std::vector<std::string> names;
    names.reserve(items.size());
    for (const T& item : items) {
        names.push_back(item.name);
    }
    return names;
}

} // namespace ge::game::detail
