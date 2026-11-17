#pragma once

#include "ge/ecs/entity.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ge::ecs {

/// Hands out entity handles and recycles their slots with a new generation.
class EntityAllocator {
public:
    /// Returns a live entity, reusing a freed slot when one exists.
    [[nodiscard]] Entity allocate();

    /// Frees the entity's slot. Returns false if the handle was already stale.
    bool release(Entity entity);

    /// Whether the handle refers to a live entity.
    [[nodiscard]] bool contains(Entity entity) const noexcept;

    /// Number of live entities.
    [[nodiscard]] std::size_t size() const noexcept { return generations_.size() - free_.size(); }

    /// Snapshot of every live entity, ordered by slot index.
    [[nodiscard]] std::vector<Entity> alive() const;

private:
    std::vector<std::uint32_t> generations_;
    std::vector<std::uint8_t> alive_;
    std::vector<std::uint32_t> free_;
};

} // namespace ge::ecs
