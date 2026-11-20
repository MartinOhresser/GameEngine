#pragma once

// Finding entities by the name they answer to (a Tag). Header only, so that a module that depends
// on tag uses it without a call into another module.

#include "ge/ecs/entity.hpp"
#include "ge/ecs/world.hpp"
#include "ge/mod/tag/tag.gen.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace ge::mod::tag {

/// The entity that answers to a tag: the oldest one when several share it, the null entity when
/// none does. For one look-up; Tagged is faster for many.
[[nodiscard]] inline ecs::Entity find(ecs::World& world, std::uint32_t name) {
    ecs::Entity found;
    world.each<const Tag>([&found, name](ecs::Entity entity, const Tag& item) {
        if (found.is_null() && item.tag == name) {
            found = entity;
        }
    });
    return found;
}

/// Every tag in use and the entity that answers to it, read once for many look-ups in a tick.
class Tagged {
public:
    explicit Tagged(ecs::World& world) {
        world.each<const Tag>([this](ecs::Entity entity, const Tag& item) {
            if (find(item.tag).is_null()) {
                entries_.emplace_back(item.tag, entity);
            }
        });
    }

    /// The entity that answers to this tag (the oldest), or the null entity.
    [[nodiscard]] ecs::Entity find(std::uint32_t name) const {
        const auto entry =
            std::ranges::find_if(entries_, [name](const auto& item) { return item.first == name; });
        return entry == entries_.end() ? ecs::Entity{} : entry->second;
    }

private:
    std::vector<std::pair<std::uint32_t, ecs::Entity>> entries_;
};

/// Every tag in use and the entity that answers to it.
[[nodiscard]] inline Tagged tagged(ecs::World& world) {
    return Tagged(world);
}

} // namespace ge::mod::tag
