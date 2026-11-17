#pragma once

#include "ge/core/hash.hpp"

#include <cstdint>

namespace ge {

/// Deterministic pseudo-random generator (PCG32, XSH-RR variant).
///
/// The same seed gives the same sequence on every platform and compiler, which
/// is what replays, save games and network prediction rely on. Nothing here
/// uses `<random>`: its distributions are allowed to differ between standard
/// libraries.
///
/// The whole state is two integers (`state()` and `stream()`), so it can be
/// copied, stored in a snapshot and restored with `from_state`.
class Random {
public:
    /// `seed` picks the starting point; `stream` picks one of 2^63 unrelated
    /// sequences, so two systems can draw independently from the same seed.
    explicit Random(std::uint64_t seed = 0, std::uint64_t stream = 0) noexcept;

    /// Rebuilds a generator from values returned by `state()` and `stream()`.
    [[nodiscard]] static Random from_state(std::uint64_t state, std::uint64_t stream) noexcept;

    [[nodiscard]] std::uint32_t next_u32() noexcept;
    [[nodiscard]] std::uint64_t next_u64() noexcept;

    /// Uniform integer in [0, bound). Returns 0 when `bound` is 0.
    [[nodiscard]] std::uint32_t below(std::uint32_t bound) noexcept;

    /// Uniform integer in [low, high], both included. `low` must not exceed `high`.
    [[nodiscard]] std::int32_t range(std::int32_t low, std::int32_t high) noexcept;

    /// Uniform float in [0, 1).
    [[nodiscard]] float unit_float() noexcept;

    /// True with probability `probability` (values outside [0, 1] are clamped by use).
    [[nodiscard]] bool chance(float probability) noexcept;

    /// A new generator seeded from this one, for handing a private sequence to a subsystem.
    /// Advances this generator.
    [[nodiscard]] Random split() noexcept;

    [[nodiscard]] std::uint64_t state() const noexcept { return state_; }
    [[nodiscard]] std::uint64_t stream() const noexcept { return (increment_ - 1U) >> 1U; }

    void hash(Hasher& hasher) const {
        hasher.add(state_);
        hasher.add(increment_);
    }

    [[nodiscard]] friend bool operator==(const Random&, const Random&) noexcept = default;

private:
    std::uint64_t state_ = 0;
    std::uint64_t increment_ = 1;
};

} // namespace ge
