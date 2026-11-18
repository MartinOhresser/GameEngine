#pragma once

#include "ge/app/input.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace ge::platform {

/// A raw input change, as a backend reports it: which control, and its value.
///
/// A source is "kind:name" in lower case: "key:space", "key:left",
/// "mouse:left". A button is 0 (up) or 1 (down). Kinds: `key`, `mouse`; `pad`
/// and `touch` are reserved for later backends and already accepted in files.
struct RawEvent {
    std::string source;
    float value = 0.0F;
};

/// The latest value of every raw source seen so far.
class RawState {
public:
    void set(std::string_view source, float value);
    /// The value of the source, 0 if it was never set.
    [[nodiscard]] float value(std::string_view source) const;

private:
    std::map<std::string, float, std::less<>> values_;
};

/// One line of the input file: this source drives this action, scaled.
struct Binding {
    std::string action;
    std::string source;
    float scale = 1.0F;
};

/// Which raw controls drive which actions: the only place that knows keys.
///
/// An action takes the sum of `scale * value` over its bindings, kept within
/// [-1, 1]: "right" with scale 1 and "left" with scale -1 on `move_x` make an
/// axis; two keys bound the same way never go beyond 1. Game logic reads the
/// actions and never learns which backend or which key produced them.
class InputMap {
public:
    void bind(std::string action, std::string source, float scale);

    [[nodiscard]] const std::vector<Binding>& bindings() const noexcept { return bindings_; }

    /// Sets, in `live`, every action that has a binding.
    void apply(const RawState& raw, app::LiveInput& live) const;

private:
    std::vector<Binding> bindings_;
};

/// Whether `source` has the "kind:name" form with a known kind.
[[nodiscard]] bool is_valid_source(std::string_view source);

/// A problem in an input file: where (`path` inside the document) and why.
struct InputProblem {
    std::string file;
    std::string path;
    std::string message;

    /// "file: path: message".
    [[nodiscard]] std::string to_string() const { return file + ": " + path + ": " + message; }
};

struct InputMapResult {
    InputMap map;
    /// Empty when the file is valid.
    std::vector<InputProblem> problems;

    [[nodiscard]] bool ok() const noexcept { return problems.empty(); }
};

/// Reads the text of an input file (relaxed JSON):
///
///     { "schema_version": 1,
///       "bindings": [ { "action": "move_x", "sources": { "key:right": 1, "key:left": -1 } } ] }
///
/// `file` labels the problems.
[[nodiscard]] InputMapResult parse_input_map(std::string_view text, std::string_view file);

} // namespace ge::platform
