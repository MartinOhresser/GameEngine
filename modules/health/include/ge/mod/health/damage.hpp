#pragma once

// How a module hurts an entity without knowing what it is: it asks for damage. The health module
// applies the request at its next system run. Header only, for the modules that depend on health.

#include "ge/ecs/commands.hpp"
#include "ge/ecs/world.hpp"
#include "ge/mod/health/health.gen.hpp"

namespace ge::mod::health {

/// Asks for `amount` points of damage on `entity` (a negative amount heals). Requests of one tick
/// add up: if the entity already has a Damage, the amount is added to it.
inline void deal(ecs::World& world, ecs::Entity entity, std::int32_t amount) {
    if (auto* pending = world.get<Damage>(entity)) {
        pending->amount += amount;
    } else {
        world.resource<ecs::Commands>()->insert(entity, Damage{amount});
    }
}

} // namespace ge::mod::health
