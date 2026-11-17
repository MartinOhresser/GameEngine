#include "ge/core/random.hpp"

#include "../support/mini_test.hpp"

#include <array>
#include <cstdint>
#include <limits>

using ge::Random;

TEST_CASE("random: matches the reference PCG32 sequence") {
    // Reference values from the PCG authors' demo program: seed 42, stream 54.
    constexpr std::array<std::uint32_t, 6> expected{0xa15c02b7U, 0x7b47f409U, 0xba1d3330U,
                                                    0x83d2f293U, 0xbfa4784bU, 0xcbed606eU};
    Random random(42, 54);
    for (const std::uint32_t value : expected) {
        CHECK(random.next_u32() == value);
    }
}

TEST_CASE("random: the same seed gives the same sequence") {
    Random a(1234);
    Random b(1234);
    bool all_equal = true;
    for (int i = 0; i < 100; ++i) {
        all_equal = all_equal && a.next_u32() == b.next_u32();
    }
    CHECK(all_equal);
    CHECK(a == b);
}

TEST_CASE("random: different seeds and different streams differ") {
    Random base(1);
    Random other_seed(2);
    Random other_stream(1, 1);
    CHECK(base.next_u32() != other_seed.next_u32());
    Random base_again(1);
    CHECK(base_again.next_u32() != other_stream.next_u32());
}

TEST_CASE("random: state can be saved and restored") {
    Random original(99, 7);
    for (int i = 0; i < 10; ++i) {
        static_cast<void>(original.next_u32());
    }
    CHECK(original.stream() == 7);

    Random restored = Random::from_state(original.state(), original.stream());
    CHECK(restored == original);
    for (int i = 0; i < 10; ++i) {
        CHECK(restored.next_u32() == original.next_u32());
    }
}

TEST_CASE("random: next_u64 joins two draws, high word first") {
    Random a(5);
    Random b(5);
    const std::uint64_t high = a.next_u32();
    const std::uint64_t low = a.next_u32();
    CHECK(b.next_u64() == ((high << 32U) | low));
}

TEST_CASE("random: below stays under the bound and reaches every value") {
    Random random(3);
    CHECK(random.below(0) == 0);
    CHECK(random.below(1) == 0);

    std::array<int, 6> seen{};
    for (int i = 0; i < 600; ++i) {
        const std::uint32_t value = random.below(6);
        REQUIRE(value < 6);
        ++seen.at(value);
    }
    for (const int count : seen) {
        CHECK(count > 0);
    }
}

TEST_CASE("random: below is close to uniform") {
    Random random(2024);
    std::array<int, 4> buckets{};
    for (int i = 0; i < 40000; ++i) {
        ++buckets.at(random.below(4));
    }
    for (const int count : buckets) {
        CHECK(count > 9000);
        CHECK(count < 11000);
    }
}

TEST_CASE("random: range includes both ends") {
    Random random(11);
    bool saw_low = false;
    bool saw_high = false;
    for (int i = 0; i < 500; ++i) {
        const std::int32_t value = random.range(-2, 2);
        REQUIRE(value >= -2);
        REQUIRE(value <= 2);
        saw_low = saw_low || value == -2;
        saw_high = saw_high || value == 2;
    }
    CHECK(saw_low);
    CHECK(saw_high);
    CHECK(random.range(5, 5) == 5);
}

TEST_CASE("random: range handles the full integer span") {
    Random random(8);
    constexpr auto lowest = std::numeric_limits<std::int32_t>::min();
    constexpr auto highest = std::numeric_limits<std::int32_t>::max();
    bool differs = false;
    const std::int32_t first = random.range(lowest, highest);
    for (int i = 0; i < 10; ++i) {
        differs = differs || random.range(lowest, highest) != first;
    }
    CHECK(differs);
}

TEST_CASE("random: unit_float is in [0, 1) and centred") {
    Random random(77);
    double sum = 0.0;
    bool in_range = true;
    for (int i = 0; i < 10000; ++i) {
        const float value = random.unit_float();
        in_range = in_range && value >= 0.0F && value < 1.0F;
        sum += static_cast<double>(value);
    }
    CHECK(in_range);
    CHECK(sum / 10000.0 > 0.48);
    CHECK(sum / 10000.0 < 0.52);
}

TEST_CASE("random: chance of 0 never happens and chance of 1 always does") {
    Random random(5);
    bool ever = false;
    bool always = true;
    for (int i = 0; i < 1000; ++i) {
        ever = ever || random.chance(0.0F);
        always = always && random.chance(1.0F);
    }
    CHECK(!ever);
    CHECK(always);
}

TEST_CASE("random: split gives a private, reproducible generator") {
    Random parent_a(500);
    Random parent_b(500);
    Random child_a = parent_a.split();
    const Random child_b = parent_b.split();
    CHECK(child_a == child_b);
    CHECK(parent_a == parent_b);

    const Random untouched(500);
    CHECK(!(parent_a == untouched)); // splitting advanced the parent
    CHECK(child_a.next_u32() != parent_a.next_u32());
}
