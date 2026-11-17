#pragma once

#include <cstdint>

namespace ge::ecs {

/// Handle to an entity: a slot index plus a generation.
///
/// When an entity is destroyed its slot is recycled with a new generation, so a
/// stale handle never aliases the entity that reuses the slot. A
/// default-constructed handle is the null entity and is never alive.
class Entity {
public:
    static constexpr std::uint32_t null_index = 0xFFFFFFFFU;

    constexpr Entity() noexcept = default;
    constexpr Entity(std::uint32_t index, std::uint32_t generation) noexcept
        : index_(index), generation_(generation) {}

    /// Slot index. Indices are reused after an entity is destroyed.
    [[nodiscard]] constexpr std::uint32_t index() const noexcept { return index_; }

    /// Generation of the slot when this handle was created.
    [[nodiscard]] constexpr std::uint32_t generation() const noexcept { return generation_; }

    [[nodiscard]] constexpr bool is_null() const noexcept { return index_ == null_index; }

    /// Packs the handle into one integer, for logging or network messages.
    [[nodiscard]] constexpr std::uint64_t to_bits() const noexcept {
        return (static_cast<std::uint64_t>(generation_) << 32U) | index_;
    }

    friend constexpr bool operator==(const Entity&, const Entity&) noexcept = default;

private:
    std::uint32_t index_ = null_index;
    std::uint32_t generation_ = 0;
};

} // namespace ge::ecs
