#pragma once

#include "ge/ecs/entity.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace ge::ecs {

/// Type-erased face of a component storage, so the world can destroy an entity
/// without knowing the component types it carries.
class StorageBase {
public:
    StorageBase() = default;
    StorageBase(const StorageBase&) = delete;
    StorageBase& operator=(const StorageBase&) = delete;
    StorageBase(StorageBase&&) = delete;
    StorageBase& operator=(StorageBase&&) = delete;
    virtual ~StorageBase() = default;

    /// Removes the component of the entity in slot `index`, if there is one.
    virtual void remove_entity(std::uint32_t index) = 0;
};

/// Components of one type, packed in a dense array with a sparse index by
/// entity slot. Insertion, lookup and removal are O(1); iteration walks
/// contiguous memory. Removal swaps the last element into the hole.
template <typename T>
class SparseSet final : public StorageBase {
    static_assert(std::is_move_constructible_v<T> && std::is_move_assignable_v<T>,
                  "components must be movable");

public:
    /// Stores a component for `entity`, replacing the one it already had.
    template <typename... Args>
    T& emplace(Entity entity, Args&&... args) {
        const std::size_t index = entity.index();
        if (index >= sparse_.size()) {
            sparse_.resize(index + 1, absent);
        }
        if (const std::uint32_t existing = sparse_[index]; existing != absent) {
            entities_[existing] = entity;
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay): string literals
            data_[existing] = T(std::forward<Args>(args)...);
            return data_[existing];
        }
        sparse_[index] = static_cast<std::uint32_t>(data_.size());
        entities_.push_back(entity);
        return data_.emplace_back(std::forward<Args>(args)...);
    }

    [[nodiscard]] T* get(Entity entity) noexcept {
        const std::size_t slot = slot_of(entity);
        return slot == npos ? nullptr : &data_[slot];
    }

    [[nodiscard]] const T* get(Entity entity) const noexcept {
        const std::size_t slot = slot_of(entity);
        return slot == npos ? nullptr : &data_[slot];
    }

    /// Removes the entity's component. Returns false if it had none.
    bool remove(Entity entity) {
        const std::size_t slot = slot_of(entity);
        if (slot == npos) {
            return false;
        }
        erase_slot(slot);
        return true;
    }

    void remove_entity(std::uint32_t index) override {
        if (index < sparse_.size() && sparse_[index] != absent) {
            erase_slot(sparse_[index]);
        }
    }

    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }

    /// Entities that have this component, in storage order.
    [[nodiscard]] std::span<const Entity> entities() const noexcept { return entities_; }

    /// Components, in the same order as `entities()`.
    [[nodiscard]] std::span<T> values() noexcept { return data_; }
    [[nodiscard]] std::span<const T> values() const noexcept { return data_; }

private:
    static constexpr std::uint32_t absent = std::numeric_limits<std::uint32_t>::max();
    static constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();

    [[nodiscard]] std::size_t slot_of(Entity entity) const noexcept {
        const std::size_t index = entity.index();
        if (index >= sparse_.size()) {
            return npos;
        }
        const std::uint32_t slot = sparse_[index];
        if (slot == absent || entities_[slot] != entity) {
            return npos;
        }
        return slot;
    }

    void erase_slot(std::size_t slot) {
        sparse_[entities_[slot].index()] = absent;
        if (slot + 1 != data_.size()) {
            entities_[slot] = entities_.back();
            data_[slot] = std::move(data_.back());
            sparse_[entities_[slot].index()] = static_cast<std::uint32_t>(slot);
        }
        entities_.pop_back();
        data_.pop_back();
    }

    std::vector<std::uint32_t> sparse_;
    std::vector<Entity> entities_;
    std::vector<T> data_;
};

} // namespace ge::ecs
