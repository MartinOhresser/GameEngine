#include "ge/ecs/entity_allocator.hpp"

#include "ge/core/assert.hpp"

namespace ge::ecs {

Entity EntityAllocator::allocate() {
    if (!free_.empty()) {
        const std::uint32_t index = free_.back();
        free_.pop_back();
        alive_[index] = 1;
        return {index, generations_[index]};
    }
    GE_ASSERT(generations_.size() < Entity::null_index);
    const auto index = static_cast<std::uint32_t>(generations_.size());
    generations_.push_back(0);
    alive_.push_back(1);
    return {index, 0};
}

bool EntityAllocator::release(Entity entity) {
    if (!contains(entity)) {
        return false;
    }
    const std::uint32_t index = entity.index();
    alive_[index] = 0;
    ++generations_[index]; // unsigned wrap-around is well defined
    free_.push_back(index);
    return true;
}

bool EntityAllocator::contains(Entity entity) const noexcept {
    const std::size_t index = entity.index();
    return index < generations_.size() && alive_[index] != 0 &&
           generations_[index] == entity.generation();
}

std::vector<Entity> EntityAllocator::alive() const {
    std::vector<Entity> result;
    result.reserve(size());
    for (std::size_t index = 0; index < generations_.size(); ++index) {
        if (alive_[index] != 0) {
            result.emplace_back(static_cast<std::uint32_t>(index), generations_[index]);
        }
    }
    return result;
}

} // namespace ge::ecs
