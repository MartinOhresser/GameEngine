#include "ge/core/hash.hpp"

#include "../support/mini_test.hpp"

#include <array>
#include <cstdint>
#include <string_view>

using ge::Hasher;

namespace {

enum class Color : std::uint8_t { Red, Green };

struct Pair {
    std::uint32_t a;
    std::uint32_t b;
};

struct WithMember {
    float x = 0.0F;
    void hash(Hasher& hasher) const { hasher.add(x); }
};

struct Padded {
    char c;
    std::uint64_t n;
};

} // namespace

TEST_CASE("hash: same input, same value; different input, different value") {
    CHECK(ge::hash_of(std::uint32_t{7}) == ge::hash_of(std::uint32_t{7}));
    CHECK(ge::hash_of(std::uint32_t{7}) != ge::hash_of(std::uint32_t{8}));
}

TEST_CASE("hash: the order of additions matters") {
    Hasher ab;
    ab.add(std::uint32_t{1});
    ab.add(std::uint32_t{2});
    Hasher ba;
    ba.add(std::uint32_t{2});
    ba.add(std::uint32_t{1});
    CHECK(ab.value() != ba.value());
}

TEST_CASE("hash: integers are hashed by value, whatever their width is written as") {
    // Little-endian byte order is fixed, so this value is the same everywhere.
    Hasher hasher;
    hasher.add(std::uint32_t{0x01020304U});
    Hasher bytes;
    const std::array<std::byte, 4> raw = {std::byte{4}, std::byte{3}, std::byte{2}, std::byte{1}};
    bytes.add_bytes(raw);
    CHECK(hasher.value() == bytes.value());
}

TEST_CASE("hash: bool, enum, floats, doubles and negative numbers") {
    CHECK(ge::hash_of(true) != ge::hash_of(false));
    CHECK(ge::hash_of(Color::Red) != ge::hash_of(Color::Green));
    CHECK(ge::hash_of(1.5F) == ge::hash_of(1.5F));
    CHECK(ge::hash_of(1.5F) != ge::hash_of(2.5F));
    CHECK(ge::hash_of(1.5) != ge::hash_of(2.5));
    CHECK(ge::hash_of(std::int32_t{-1}) != ge::hash_of(std::int32_t{1}));
}

TEST_CASE("hash: strings carry their length") {
    Hasher split_early;
    split_early.add(std::string_view("ab"));
    split_early.add(std::string_view("c"));
    Hasher split_late;
    split_late.add(std::string_view("a"));
    split_late.add(std::string_view("bc"));
    CHECK(split_early.value() != split_late.value());
}

TEST_CASE("hash: structs without padding hash their bytes, others use a member") {
    CHECK(ge::hash_of(Pair{1, 2}) == ge::hash_of(Pair{1, 2}));
    CHECK(ge::hash_of(Pair{1, 2}) != ge::hash_of(Pair{2, 1}));
    CHECK(ge::hash_of(WithMember{1.0F}) == ge::hash_of(1.0F));
    static_assert(ge::Hashable<Pair>);
    static_assert(ge::Hashable<WithMember>);
    static_assert(!ge::Hashable<Padded>);
    static_assert(!ge::Hashable<int*>);
}
