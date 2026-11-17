#include "ge/core/random.hpp"

#include "ge/core/assert.hpp"

namespace ge {

namespace {

constexpr std::uint64_t multiplier = 6364136223846793005ULL;

} // namespace

Random::Random(std::uint64_t seed, std::uint64_t stream) noexcept
    : increment_((stream << 1U) | 1U) {
    static_cast<void>(next_u32());
    state_ += seed;
    static_cast<void>(next_u32());
}

Random Random::from_state(std::uint64_t state, std::uint64_t stream) noexcept {
    Random random;
    random.state_ = state;
    random.increment_ = (stream << 1U) | 1U;
    return random;
}

std::uint32_t Random::next_u32() noexcept {
    const std::uint64_t old = state_;
    state_ = old * multiplier + increment_;
    const auto xorshifted = static_cast<std::uint32_t>(((old >> 18U) ^ old) >> 27U);
    const auto rotation = static_cast<std::uint32_t>(old >> 59U);
    return (xorshifted >> rotation) | (xorshifted << ((~rotation + 1U) & 31U));
}

std::uint64_t Random::next_u64() noexcept {
    const std::uint64_t high = next_u32();
    const std::uint64_t low = next_u32();
    return (high << 32U) | low;
}

std::uint32_t Random::below(std::uint32_t bound) noexcept {
    if (bound == 0) {
        return 0;
    }
    // Rejecting the few values below `threshold` removes modulo bias.
    const std::uint32_t threshold = (~bound + 1U) % bound;
    for (;;) {
        const std::uint32_t value = next_u32();
        if (value >= threshold) {
            return value % bound;
        }
    }
}

std::int32_t Random::range(std::int32_t low, std::int32_t high) noexcept {
    GE_ASSERT(low <= high);
    const auto span = static_cast<std::uint32_t>(static_cast<std::int64_t>(high) - low + 1);
    // A span of 2^32 (the full int range) wraps to 0 and means "any value".
    const std::uint32_t offset = span == 0 ? next_u32() : below(span);
    return static_cast<std::int32_t>(static_cast<std::int64_t>(low) + offset);
}

float Random::unit_float() noexcept {
    // 24 bits fill a float's mantissa exactly, so every result is representable.
    return static_cast<float>(next_u32() >> 8U) * (1.0F / 16777216.0F);
}

bool Random::chance(float probability) noexcept {
    return unit_float() < probability;
}

Random Random::split() noexcept {
    const std::uint64_t seed = next_u64();
    const std::uint64_t stream = next_u64();
    return Random(seed, stream);
}

} // namespace ge
