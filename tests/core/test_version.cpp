#include "ge/core/version.hpp"

#include "../support/mini_test.hpp"

TEST_CASE("core: version matches the CMake project version") {
    CHECK(ge::version() == GE_EXPECTED_VERSION);
}

TEST_CASE("core: version has the major.minor.patch shape") {
    const std::string_view version = ge::version();
    int dots = 0;
    for (const char c : version) {
        if (c == '.') {
            ++dots;
        } else {
            const bool is_digit = c >= '0' && c <= '9';
            CHECK(is_digit);
        }
    }
    CHECK(dots == 2);
}
