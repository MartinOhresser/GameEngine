#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ge::json {

/// A JSON value. Objects keep the order of their members, so parsing a file and
/// writing it back never reorders it, and generated output is deterministic.
///
/// Numbers that are written without a fraction or exponent and fit in 64 bits
/// are integers (`Kind::Int`); every other number is a double (`Kind::Real`).
class Value {
public:
    enum class Kind : std::uint8_t { Null, Bool, Int, Real, String, Array, Object };

    using Array = std::vector<Value>;
    using Member = std::pair<std::string, Value>;
    using Object = std::vector<Member>;

    Value() = default;

    [[nodiscard]] static Value boolean(bool value);
    [[nodiscard]] static Value integer(std::int64_t value);
    [[nodiscard]] static Value real(double value);
    [[nodiscard]] static Value string(std::string value);
    [[nodiscard]] static Value array(Array items = {});
    [[nodiscard]] static Value object(Object members = {});

    [[nodiscard]] Kind kind() const noexcept { return kind_; }
    [[nodiscard]] bool is_null() const noexcept { return kind_ == Kind::Null; }
    [[nodiscard]] bool is_bool() const noexcept { return kind_ == Kind::Bool; }
    [[nodiscard]] bool is_int() const noexcept { return kind_ == Kind::Int; }
    /// True for integers and reals.
    [[nodiscard]] bool is_number() const noexcept {
        return kind_ == Kind::Int || kind_ == Kind::Real;
    }
    [[nodiscard]] bool is_string() const noexcept { return kind_ == Kind::String; }
    [[nodiscard]] bool is_array() const noexcept { return kind_ == Kind::Array; }
    [[nodiscard]] bool is_object() const noexcept { return kind_ == Kind::Object; }

    /// Typed access. Each one returns a neutral value (false, 0, empty) when the
    /// kind does not match, so callers check the kind first.
    [[nodiscard]] bool as_bool() const noexcept { return bool_; }
    [[nodiscard]] std::int64_t as_int() const noexcept { return int_; }
    /// The number as a double (integers are converted).
    [[nodiscard]] double as_real() const noexcept;
    [[nodiscard]] const std::string& as_string() const noexcept { return string_; }
    [[nodiscard]] const Array& items() const noexcept { return items_; }
    [[nodiscard]] const Object& members() const noexcept { return members_; }

    /// The member with this key, or null. Only meaningful for objects.
    [[nodiscard]] const Value* find(std::string_view key) const noexcept;

    /// Appends to an array.
    void push_back(Value value);
    /// Adds a member to an object, or replaces the one with the same key.
    void set(std::string key, Value value);

    [[nodiscard]] friend bool operator==(const Value&, const Value&) = default;

private:
    Kind kind_ = Kind::Null;
    bool bool_ = false;
    std::int64_t int_ = 0;
    double real_ = 0.0;
    std::string string_;
    Array items_;
    Object members_;
};

/// Where and why a document failed to parse. Lines and columns start at 1.
struct ParseError {
    std::size_t line = 1;
    std::size_t column = 1;
    std::string message;

    /// "line:column: message".
    [[nodiscard]] std::string to_string() const;
};

struct ParseResult {
    Value value;
    std::optional<ParseError> error;

    [[nodiscard]] bool ok() const noexcept { return !error.has_value(); }
};

/// Parses a complete JSON document. Strict: no comments, no trailing commas,
/// no duplicate keys, nesting limited to 128 levels.
[[nodiscard]] ParseResult parse(std::string_view text);

/// The shortest text that reads back as exactly `value` (and always looks like
/// a real: "3.0", never "3"). Non-finite values give "0.0".
[[nodiscard]] std::string format_real(double value);
[[nodiscard]] std::string format_real(float value);

/// Writes a value as JSON. `indent` is the number of spaces per level; 0 gives
/// a compact one-line form. Non-finite reals are written as null.
[[nodiscard]] std::string dump(const Value& value, int indent = 2);

} // namespace ge::json
