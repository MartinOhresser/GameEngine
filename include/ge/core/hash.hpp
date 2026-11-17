#pragma once

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace ge {

class Hasher;

/// A type that has a `void hash(Hasher&) const` member.
template <typename T>
concept HasHashMember = requires(const T& value, Hasher& hasher) { value.hash(hasher); };

/// A type `Hasher::add` accepts: it has a `hash` member, is a number or an
/// enum, or is a trivially copyable type whose bytes are all meaningful (no
/// padding, no floating point members, no pointers: when in doubt, write a
/// `hash` member).
template <typename T>
concept Hashable = HasHashMember<T> || std::integral<T> || std::is_enum_v<T> ||
                   (std::floating_point<T> && (sizeof(T) == 4U || sizeof(T) == 8U)) ||
                   (std::is_trivially_copyable_v<T> &&
                    std::has_unique_object_representations_v<T> && !std::is_pointer_v<T>);

/// 64-bit FNV-1a with a final avalanche step.
///
/// Integers are fed byte by byte, least significant first, so the value is the
/// same on every platform for fixed-width types. Use `std::uint32_t` and
/// friends rather than `std::size_t`, whose width differs between platforms.
/// Floats and doubles are hashed by their bit pattern.
///
/// The simulation fingerprint (`World::fingerprint`) is built from this: two
/// runs of a deterministic simulation must produce the same value, and any
/// divergence should change it.
class Hasher {
public:
    void add_bytes(std::span<const std::byte> bytes) noexcept {
        for (const std::byte byte : bytes) {
            state_ = (state_ ^ static_cast<std::uint64_t>(byte)) * prime;
        }
    }

    /// Adds a string, length first so "ab","c" and "a","bc" differ.
    void add(std::string_view text) noexcept {
        add(static_cast<std::uint64_t>(text.size()));
        add_bytes(std::as_bytes(std::span<const char>(text)));
    }

    template <Hashable T>
    void add(const T& value) noexcept(!HasHashMember<T>) {
        if constexpr (HasHashMember<T>) {
            value.hash(*this);
        } else if constexpr (std::same_as<T, bool>) {
            add_integer(static_cast<std::uint8_t>(value ? 1U : 0U));
        } else if constexpr (std::integral<T>) {
            add_integer(static_cast<std::make_unsigned_t<T>>(value));
        } else if constexpr (std::is_enum_v<T>) {
            add(static_cast<std::underlying_type_t<T>>(value));
        } else if constexpr (std::floating_point<T> && sizeof(T) == 4U) {
            add_integer(std::bit_cast<std::uint32_t>(value));
        } else if constexpr (std::floating_point<T>) {
            add_integer(std::bit_cast<std::uint64_t>(value));
        } else {
            add_bytes(std::as_bytes(std::span<const T, 1>(&value, 1)));
        }
    }

    /// The hash of everything added so far. Does not reset the hasher.
    [[nodiscard]] std::uint64_t value() const noexcept {
        std::uint64_t mixed = state_;
        mixed ^= mixed >> 30U;
        mixed *= 0xBF58476D1CE4E5B9ULL;
        mixed ^= mixed >> 27U;
        mixed *= 0x94D049BB133111EBULL;
        mixed ^= mixed >> 31U;
        return mixed;
    }

private:
    static constexpr std::uint64_t offset_basis = 14695981039346656037ULL;
    static constexpr std::uint64_t prime = 1099511628211ULL;

    template <std::unsigned_integral U>
    void add_integer(U value) noexcept {
        for (std::size_t i = 0; i < sizeof(U); ++i) {
            const auto byte =
                static_cast<std::uint8_t>((static_cast<std::uint64_t>(value) >> (8U * i)) & 0xFFU);
            state_ = (state_ ^ byte) * prime;
        }
    }

    std::uint64_t state_ = offset_basis;
};

/// Hash of a single value, for convenience.
template <Hashable T>
[[nodiscard]] std::uint64_t hash_of(const T& value) {
    Hasher hasher;
    hasher.add(value);
    return hasher.value();
}

} // namespace ge
