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
        return;
    }
    // Not yet there: when the request is queued, another one may be queued before it is applied
    // (two monsters reaching the base in the same tick), so the merge happens at apply time.
    world.resource<ecs::Commands>()->push([entity, amount](ecs::World& target) {
        if (auto* pending = target.get<Damage>(entity)) {
            pending->amount += amount;
        } else {
            static_cast<void>(target.insert(entity, Damage{amount}));
        }
    });
}

} // namespace ge::mod::health
