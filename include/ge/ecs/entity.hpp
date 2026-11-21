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

    /// The handle as a number a component field (`u64`) can hold, where 0 means "no
    /// entity": a link from one entity to another (an owner, a target). The generation
    /// travels with it, so a link to an entity that is gone is never mistaken for the one
    /// that reuses its slot: check it with `World::is_alive` after `from_link`.
    [[nodiscard]] constexpr std::uint64_t to_link() const noexcept {
        return is_null() ? 0U : to_bits() + 1U;
    }

    /// The entity a link holds; the null entity for 0.
    [[nodiscard]] static constexpr Entity from_link(std::uint64_t link) noexcept {
        if (link == 0U) {
            return Entity{};
        }
        const std::uint64_t bits = link - 1U;
        return Entity{static_cast<std::uint32_t>(bits & 0xFFFFFFFFU),
                      static_cast<std::uint32_t>(bits >> 32U)};
    }

    friend constexpr bool operator==(const Entity&, const Entity&) noexcept = default;

private:
    std::uint32_t index_ = null_index;
    std::uint32_t generation_ = 0;
};

} // namespace ge::ecs
