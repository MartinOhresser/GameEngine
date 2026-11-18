#include "ge/app/text.hpp"

#include "../support/mini_test.hpp"

TEST_CASE("text: UTF-8 is decoded to characters, whatever their length in bytes") {
    CHECK(ge::app::decode_utf8("abc") == U"abc");
    CHECK(ge::app::decode_utf8("Caf\xC3\xA9") == U"Café");            // two bytes
    CHECK(ge::app::decode_utf8("\xE2\x82\xAC") == U"€");              // three bytes
    CHECK(ge::app::decode_utf8("\xF0\x9F\x98\x80") == U"\U0001F600"); // four bytes
    CHECK(ge::app::decode_utf8("").empty());
}

TEST_CASE("text: broken UTF-8 becomes replacement characters instead of failing") {
    CHECK(ge::app::decode_utf8("a\x80z") == U"a�z"); // a stray continuation byte
    CHECK(ge::app::decode_utf8("a\xC3") == U"a�");   // cut in the middle
    CHECK(ge::app::decode_utf8("\xC3(") == U"�(");   // a bad continuation
}
