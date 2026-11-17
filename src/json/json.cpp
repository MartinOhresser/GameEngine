#include "ge/json/json.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <system_error>

namespace ge::json {

// ---- Value -----------------------------------------------------------------

Value Value::boolean(bool value) {
    Value result;
    result.kind_ = Kind::Bool;
    result.bool_ = value;
    return result;
}

Value Value::integer(std::int64_t value) {
    Value result;
    result.kind_ = Kind::Int;
    result.int_ = value;
    return result;
}

Value Value::real(double value) {
    Value result;
    result.kind_ = Kind::Real;
    result.real_ = value;
    return result;
}

Value Value::string(std::string value) {
    Value result;
    result.kind_ = Kind::String;
    result.string_ = std::move(value);
    return result;
}

Value Value::array(Array items) {
    Value result;
    result.kind_ = Kind::Array;
    result.items_ = std::move(items);
    return result;
}

Value Value::object(Object members) {
    Value result;
    result.kind_ = Kind::Object;
    result.members_ = std::move(members);
    return result;
}

double Value::as_real() const noexcept {
    return kind_ == Kind::Int ? static_cast<double>(int_) : real_;
}

const Value* Value::find(std::string_view key) const noexcept {
    for (const Member& member : members_) {
        if (member.first == key) {
            return &member.second;
        }
    }
    return nullptr;
}

void Value::push_back(Value value) {
    items_.push_back(std::move(value));
}

void Value::set(std::string key, Value value) {
    for (Member& member : members_) {
        if (member.first == key) {
            member.second = std::move(value);
            return;
        }
    }
    members_.emplace_back(std::move(key), std::move(value));
}

std::string ParseError::to_string() const {
    return std::to_string(line) + ":" + std::to_string(column) + ": " + message;
}

// ---- parsing ---------------------------------------------------------------

namespace {

constexpr int max_depth = 128;

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    ParseResult run() {
        ParseResult result;
        skip_space();
        result.value = parse_value(0);
        if (!failed_) {
            skip_space();
            if (pos_ != text_.size()) {
                fail("unexpected content after the document");
            }
        }
        if (failed_) {
            result.value = Value{};
            result.error = error_;
        }
        return result;
    }

private:
    [[nodiscard]] static bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

    [[nodiscard]] bool at_end() const noexcept { return pos_ >= text_.size(); }
    [[nodiscard]] char peek() const noexcept { return at_end() ? '\0' : text_[pos_]; }

    void fail(std::string message) { fail_at(pos_, std::move(message)); }

    void fail_at(std::size_t position, std::string message) {
        if (failed_) {
            return;
        }
        failed_ = true;
        error_.message = std::move(message);
        error_.line = 1;
        error_.column = 1;
        for (std::size_t i = 0; i < position && i < text_.size(); ++i) {
            if (text_[i] == '\n') {
                ++error_.line;
                error_.column = 1;
            } else {
                ++error_.column;
            }
        }
    }

    void skip_space() {
        while (!at_end() && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' ||
                             text_[pos_] == '\r')) {
            ++pos_;
        }
    }

    bool consume(char expected) {
        if (peek() == expected && !at_end()) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool consume_word(std::string_view word) {
        if (text_.substr(pos_, word.size()) == word) {
            pos_ += word.size();
            return true;
        }
        return false;
    }

    Value parse_value(int depth) {
        if (depth > max_depth) {
            fail("nesting is too deep");
            return {};
        }
        if (at_end()) {
            fail("unexpected end of input");
            return {};
        }
        switch (peek()) {
        case '{':
            return parse_object(depth);
        case '[':
            return parse_array(depth);
        case '"': {
            std::string text = parse_string();
            return Value::string(std::move(text));
        }
        case 't':
            return consume_word("true") ? Value::boolean(true) : unexpected();
        case 'f':
            return consume_word("false") ? Value::boolean(false) : unexpected();
        case 'n':
            return consume_word("null") ? Value{} : unexpected();
        default:
            return parse_number();
        }
    }

    Value unexpected() {
        fail("unexpected character");
        return {};
    }

    Value parse_object(int depth) {
        ++pos_; // '{'
        Value object = Value::object();
        skip_space();
        if (consume('}')) {
            return object;
        }
        while (!failed_) {
            skip_space();
            if (peek() != '"') {
                fail("expected a string key");
                break;
            }
            const std::size_t key_position = pos_;
            std::string key = parse_string();
            if (failed_) {
                break;
            }
            if (object.find(key) != nullptr) {
                fail_at(key_position, "duplicate key \"" + key + "\"");
                break;
            }
            skip_space();
            if (!consume(':')) {
                fail("expected ':' after the key");
                break;
            }
            skip_space();
            Value member = parse_value(depth + 1);
            if (failed_) {
                break;
            }
            object.set(std::move(key), std::move(member));
            skip_space();
            if (consume('}')) {
                return object;
            }
            if (!consume(',')) {
                fail("expected ',' or '}'");
            }
        }
        return {};
    }

    Value parse_array(int depth) {
        ++pos_; // '['
        Value array = Value::array();
        skip_space();
        if (consume(']')) {
            return array;
        }
        while (!failed_) {
            skip_space();
            Value item = parse_value(depth + 1);
            if (failed_) {
                break;
            }
            array.push_back(std::move(item));
            skip_space();
            if (consume(']')) {
                return array;
            }
            if (!consume(',')) {
                fail("expected ',' or ']'");
            }
        }
        return {};
    }

    static void append_utf8(std::string& out, std::uint32_t code_point) {
        if (code_point < 0x80U) {
            out.push_back(static_cast<char>(code_point));
        } else if (code_point < 0x800U) {
            out.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
            out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
        } else if (code_point < 0x10000U) {
            out.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
            out.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
        } else {
            out.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
            out.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
        }
    }

    /// Four hex digits after "\u", or nothing.
    std::optional<std::uint32_t> parse_hex4() {
        if (pos_ + 4 > text_.size()) {
            fail("incomplete \\u escape");
            return std::nullopt;
        }
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_++];
            value <<= 4U;
            if (c >= '0' && c <= '9') {
                value |= static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value |= static_cast<std::uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value |= static_cast<std::uint32_t>(c - 'A' + 10);
            } else {
                fail_at(pos_ - 1, "invalid hex digit in \\u escape");
                return std::nullopt;
            }
        }
        return value;
    }

    void parse_unicode_escape(std::string& out) {
        const std::optional<std::uint32_t> first = parse_hex4();
        if (!first) {
            return;
        }
        std::uint32_t code_point = *first;
        if (code_point >= 0xD800U && code_point <= 0xDBFFU) {
            if (!(consume('\\') && consume('u'))) {
                fail("a high surrogate must be followed by a low surrogate");
                return;
            }
            const std::optional<std::uint32_t> second = parse_hex4();
            if (!second) {
                return;
            }
            if (*second < 0xDC00U || *second > 0xDFFFU) {
                fail("invalid low surrogate");
                return;
            }
            code_point = 0x10000U + ((code_point - 0xD800U) << 10U) + (*second - 0xDC00U);
        } else if (code_point >= 0xDC00U && code_point <= 0xDFFFU) {
            fail("unexpected low surrogate");
            return;
        }
        append_utf8(out, code_point);
    }

    void parse_escape(std::string& out) {
        const char c = peek();
        if (at_end()) {
            fail("unterminated string");
            return;
        }
        ++pos_;
        switch (c) {
        case '"':
        case '\\':
        case '/':
            out.push_back(c);
            break;
        case 'b':
            out.push_back('\b');
            break;
        case 'f':
            out.push_back('\f');
            break;
        case 'n':
            out.push_back('\n');
            break;
        case 'r':
            out.push_back('\r');
            break;
        case 't':
            out.push_back('\t');
            break;
        case 'u':
            parse_unicode_escape(out);
            break;
        default:
            fail_at(pos_ - 1, "invalid escape sequence");
            break;
        }
    }

    std::string parse_string() {
        ++pos_; // opening quote
        std::string out;
        while (!failed_) {
            if (at_end()) {
                fail("unterminated string");
                break;
            }
            const char c = text_[pos_++];
            if (c == '"') {
                return out;
            }
            if (c == '\\') {
                parse_escape(out);
            } else if (static_cast<unsigned char>(c) < 0x20U) {
                fail_at(pos_ - 1, "control character in string");
            } else {
                out.push_back(c);
            }
        }
        return {};
    }

    void skip_digits() {
        while (is_digit(peek()) && !at_end()) {
            ++pos_;
        }
    }

    Value parse_number() {
        const std::size_t start = pos_;
        bool integral = true;
        consume('-');
        if (consume('0')) {
            if (is_digit(peek())) {
                fail_at(start, "numbers cannot have leading zeros");
                return {};
            }
        } else if (peek() >= '1' && peek() <= '9') {
            skip_digits();
        } else {
            fail_at(start, "unexpected character");
            return {};
        }
        if (consume('.')) {
            integral = false;
            if (!is_digit(peek())) {
                fail("expected digits after the decimal point");
                return {};
            }
            skip_digits();
        }
        if (peek() == 'e' || peek() == 'E') {
            integral = false;
            ++pos_;
            if (peek() == '+' || peek() == '-') {
                ++pos_;
            }
            if (!is_digit(peek())) {
                fail("expected digits in the exponent");
                return {};
            }
            skip_digits();
        }
        const std::string_view token = text_.substr(start, pos_ - start);
        if (integral) {
            std::int64_t value = 0;
            const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
            if (result.ec == std::errc{}) {
                return Value::integer(value);
            }
        }
        double value = 0.0;
        const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
        if (result.ec != std::errc{}) {
            fail_at(start, "number out of range");
            return {};
        }
        return Value::real(value);
    }

    std::string_view text_;
    std::size_t pos_ = 0;
    bool failed_ = false;
    ParseError error_;
};

} // namespace

ParseResult parse(std::string_view text) {
    return Parser(text).run();
}

// ---- writing ---------------------------------------------------------------

namespace {

void write_string(std::string& out, const std::string& text) {
    constexpr std::string_view hex = "0123456789abcdef";
    out.push_back('"');
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20U) {
                out += "\\u00";
                out.push_back(hex[(static_cast<unsigned char>(c) >> 4U) & 0xFU]);
                out.push_back(hex[static_cast<unsigned char>(c) & 0xFU]);
            } else {
                out.push_back(c);
            }
            break;
        }
    }
    out.push_back('"');
}

void write_real(std::string& out, double value) {
    if (!std::isfinite(value)) {
        out += "null";
        return;
    }
    std::array<char, 40> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    std::string text(buffer.data(), result.ptr);
    if (text.find_first_of(".eE") == std::string::npos) {
        text += ".0";
    }
    out += text;
}

void write_value(std::string& out, const Value& value, int indent, int level) {
    const auto newline = [&](int at_level) {
        if (indent > 0) {
            out.push_back('\n');
            out.append(static_cast<std::size_t>(indent) * static_cast<std::size_t>(at_level), ' ');
        }
    };
    switch (value.kind()) {
    case Value::Kind::Null:
        out += "null";
        break;
    case Value::Kind::Bool:
        out += value.as_bool() ? "true" : "false";
        break;
    case Value::Kind::Int:
        out += std::to_string(value.as_int());
        break;
    case Value::Kind::Real:
        write_real(out, value.as_real());
        break;
    case Value::Kind::String:
        write_string(out, value.as_string());
        break;
    case Value::Kind::Array: {
        if (value.items().empty()) {
            out += "[]";
            break;
        }
        out.push_back('[');
        bool first = true;
        for (const Value& item : value.items()) {
            if (!first) {
                out.push_back(',');
            }
            first = false;
            newline(level + 1);
            write_value(out, item, indent, level + 1);
        }
        newline(level);
        out.push_back(']');
        break;
    }
    case Value::Kind::Object: {
        if (value.members().empty()) {
            out += "{}";
            break;
        }
        out.push_back('{');
        bool first = true;
        for (const Value::Member& member : value.members()) {
            if (!first) {
                out.push_back(',');
            }
            first = false;
            newline(level + 1);
            write_string(out, member.first);
            out += indent > 0 ? ": " : ":";
            write_value(out, member.second, indent, level + 1);
        }
        newline(level);
        out.push_back('}');
        break;
    }
    }
}

} // namespace

std::string dump(const Value& value, int indent) {
    std::string out;
    write_value(out, value, indent, 0);
    return out;
}

} // namespace ge::json
