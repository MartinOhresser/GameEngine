#include "ge/json/json.hpp"

#include "../support/mini_test.hpp"

#include <limits>
#include <string>

using ge::json::dump;
using ge::json::parse;
using ge::json::Value;

namespace {

bool fails_with(const char* text, const char* fragment) {
    const auto result = parse(text);
    return result.error.has_value() && result.error->message.find(fragment) != std::string::npos;
}

} // namespace

TEST_CASE("json: scalars") {
    CHECK(parse("null").value.is_null());
    CHECK(parse("true").value.as_bool());
    CHECK(!parse("false").value.as_bool());
    CHECK(parse("42").value.is_int());
    CHECK(parse("-7").value.as_int() == -7);
    CHECK(parse("1.5").value.kind() == Value::Kind::Real);
    CHECK(parse("1.5").value.as_real() == 1.5);
    CHECK(parse("2e3").value.as_real() == 2000.0);
    CHECK(parse("1E-2").value.as_real() == 0.01);
    CHECK(parse("7").value.as_real() == 7.0);
    CHECK(parse("\"hi\"").value.as_string() == "hi");
    CHECK(parse("  \n\t 3 \r\n").value.as_int() == 3);
}

TEST_CASE("json: big integers fall back to reals") {
    const auto result = parse("123456789012345678901234567890");
    REQUIRE(result.ok());
    CHECK(result.value.kind() == Value::Kind::Real);
    CHECK(parse("9223372036854775807").value.as_int() == std::numeric_limits<std::int64_t>::max());
    CHECK(!parse("1e999").ok());
}

TEST_CASE("json: strings and escapes") {
    CHECK(parse(R"("a\"b\\c\/d\b\f\n\r\t")").value.as_string() == "a\"b\\c/d\b\f\n\r\t");
    CHECK(parse(R"("\u00e9")").value.as_string() == "\xC3\xA9");
    CHECK(parse(R"("\u20ac")").value.as_string() == "\xE2\x82\xAC");
    CHECK(parse(R"("\ud83d\ude00")").value.as_string() == "\xF0\x9F\x98\x80");
    CHECK(parse(R"("\u0041")").value.as_string() == "A");
}

TEST_CASE("json: arrays and objects keep their order") {
    const auto result = parse(R"({"b": 1, "a": [1, 2, {"c": null}], "z": {}})");
    REQUIRE(result.ok());
    const Value& root = result.value;
    REQUIRE(root.is_object());
    CHECK(root.members().size() == 3U);
    CHECK(root.members()[0].first == "b");
    CHECK(root.members()[1].first == "a");
    REQUIRE(root.find("a") != nullptr);
    CHECK(root.find("a")->items().size() == 3U);
    CHECK(root.find("a")->items()[2].find("c")->is_null());
    CHECK(root.find("missing") == nullptr);
    CHECK(parse("[]").value.items().empty());
}

TEST_CASE("json: errors say where and why") {
    const auto result = parse("{\n  \"a\": 1,\n  \"b\": ?\n}");
    REQUIRE(result.error.has_value());
    CHECK(result.error->line == 3U);
    CHECK(result.error->column == 8U);
    CHECK(result.error->to_string().find("3:8") == 0U);
}

TEST_CASE("json: strict about the grammar") {
    CHECK(fails_with("", "end of input"));
    CHECK(fails_with("{\"a\": 1,}", "string key"));
    CHECK(fails_with("[1,]", "unexpected character"));
    CHECK(fails_with("[1 2]", "expected ','"));
    CHECK(fails_with("{\"a\" 1}", "':'"));
    CHECK(fails_with("{\"a\": 1 \"b\": 2}", "expected ','"));
    CHECK(fails_with("{1: 2}", "string key"));
    CHECK(fails_with("{\"a\": 1, \"a\": 2}", "duplicate key \"a\""));
    CHECK(fails_with("01", "leading zeros"));
    CHECK(fails_with("-", "unexpected character"));
    CHECK(fails_with("1.", "decimal point"));
    CHECK(fails_with("1e", "exponent"));
    CHECK(fails_with("1e+", "exponent"));
    CHECK(fails_with("truth", "unexpected character"));
    CHECK(fails_with("fals", "unexpected character"));
    CHECK(fails_with("nul", "unexpected character"));
    CHECK(fails_with("1 2", "after the document"));
    CHECK(fails_with("// c\n1", "unexpected character"));
    CHECK(fails_with("\"abc", "unterminated"));
    CHECK(fails_with("\"a\nb\"", "control character"));
    CHECK(fails_with("\"\\q\"", "invalid escape"));
    CHECK(fails_with("\"\\", "unterminated"));
    CHECK(fails_with("\"\\u12\"", "incomplete"));
    CHECK(fails_with("\"\\u12g4\"", "hex digit"));
    CHECK(fails_with("\"\\ud83d\"", "surrogate"));
    CHECK(fails_with("\"\\ud83d\\u0041\"", "low surrogate"));
    CHECK(fails_with("\"\\ude00\"", "low surrogate"));
    CHECK(fails_with("[", "end of input"));
    CHECK(fails_with("{", "string key"));
}

TEST_CASE("json: nesting depth is limited") {
    const std::string deep_ok(100, '[');
    CHECK(fails_with((deep_ok + std::string(100, ']')).c_str(), "") == false);
    const std::string too_deep = std::string(200, '[') + std::string(200, ']');
    CHECK(fails_with(too_deep.c_str(), "too deep"));
}

TEST_CASE("json: dump round-trips") {
    const char* text = R"({"name":"x","n":[1,2.5,true,null,"s"],"o":{"k":-3},"e":[],"f":{}})";
    const auto first = parse(text);
    REQUIRE(first.ok());
    CHECK(dump(first.value, 0) == text);
    const auto pretty = dump(first.value, 2);
    CHECK(pretty.find("\n  \"name\": \"x\"") != std::string::npos);
    const auto second = parse(pretty);
    REQUIRE(second.ok());
    CHECK(second.value == first.value);
}

TEST_CASE("json: dump escapes and formats numbers") {
    Value object = Value::object();
    object.set("text", Value::string("a\"b\\c\n\r\t\x01"));
    object.set("whole", Value::real(3.0));
    object.set("frac", Value::real(0.25));
    object.set("nan", Value::real(std::numeric_limits<double>::quiet_NaN()));
    CHECK(dump(object, 0) ==
          R"({"text":"a\"b\\c\n\r\t\u0001","whole":3.0,"frac":0.25,"nan":null})");
}

TEST_CASE("json: building values") {
    Value array = Value::array();
    array.push_back(Value::integer(1));
    array.push_back(Value::boolean(false));
    CHECK(dump(array, 0) == "[1,false]");
    Value object = Value::object({{"a", Value::integer(1)}});
    object.set("a", Value::integer(2));
    object.set("b", Value::string("x"));
    CHECK(dump(object, 0) == R"({"a":2,"b":"x"})");
    CHECK(Value{}.is_null());
    CHECK(Value::integer(1).is_number());
    CHECK(Value::real(1.0).is_number());
    CHECK(!Value::string("1").is_number());
    CHECK(Value::string("1").is_string());
    CHECK(array.is_array());
    CHECK(object.is_object());
    CHECK(Value::boolean(true).is_bool());
}

TEST_CASE("json: reals are written as the shortest text that reads back exactly") {
    using ge::json::format_real;
    CHECK(format_real(800.0) == "800.0");
    CHECK(format_real(0.1) == "0.1");
    CHECK(format_real(-2.5) == "-2.5");
    CHECK(format_real(0.0) == "0.0");
    CHECK(format_real(1e-7) == "1e-07");
    CHECK(format_real(1e20) == "1e+20");
    CHECK(format_real(0.1F) == "0.1");
    CHECK(format_real(1.25F) == "1.25");
    CHECK(format_real(16777216.0F) == "16777216.0");
    CHECK(format_real(std::numeric_limits<double>::infinity()) == "0.0");
    CHECK(format_real(std::numeric_limits<float>::quiet_NaN()) == "0.0");
    for (const double value :
         {0.30000000000000004, 1.0 / 3.0, 123456.789, 2.2250738585072014e-308}) {
        const auto parsed = parse(format_real(value));
        REQUIRE(parsed.ok());
        CHECK(parsed.value.as_real() == value);
    }
}
