#pragma once

// Spending from and giving to the reserves of a game. Header only, so that a module that depends
// on reserve (a power that costs, a gatherer that gives) uses it without a call into another
// module.

#include "ge/ecs/world.hpp"
#include "ge/mod/reserve/reserve.gen.hpp"

#include <algorithm>
#include <cstdint>

namespace ge::mod::reserve {

/// The Reserve of a kind: the oldest one; null when the game keeps none of that kind.
[[nodiscard]] inline Reserve* stock_of(ecs::World& world, std::uint32_t kind) {
    Reserve* found = nullptr;
    world.each<Reserve>([&found, kind](ecs::Entity, Reserve& stock) {
        if (found == nullptr && stock.kind == kind) {
            found = &stock;
        }
    });
    return found;
}

/// Whether amount can be taken from the kind's Reserve without going below its floor. A kind
/// with no Reserve costs nothing, so a game that does not count it lets everything through.
[[nodiscard]] inline bool can_spend(ecs::World& world, std::uint32_t kind, float amount) {
    const Reserve* stock = stock_of(world, kind);
    return stock == nullptr || amount <= 0.0F || stock->amount - amount >= stock->floor;
}

/// Takes amount from the kind's Reserve if it can (see can_spend); returns whether it did.
inline bool spend(ecs::World& world, std::uint32_t kind, float amount) {
    Reserve* stock = stock_of(world, kind);
    if (stock == nullptr || amount <= 0.0F) {
        return true;
    }
    if (stock->amount - amount < stock->floor) {
        return false;
    }
    stock->amount -= amount;
    return true;
}

/// Takes up to amount, never below the floor; returns what was taken.
inline float drain(ecs::World& world, std::uint32_t kind, float amount) {
    Reserve* stock = stock_of(world, kind);
    if (stock == nullptr || amount <= 0.0F) {
        return 0.0F;
    }
    const float taken = std::clamp(stock->amount - stock->floor, 0.0F, amount);
    stock->amount -= taken;
    return taken;
}

/// Gives amount to the kind's Reserve, up to its max; returns what it took in.
inline float give(ecs::World& world, std::uint32_t kind, float amount) {
    Reserve* stock = stock_of(world, kind);
    if (stock == nullptr || amount <= 0.0F) {
        return 0.0F;
    }
    const float taken = std::clamp(stock->max - stock->amount, 0.0F, amount);
    stock->amount += taken;
    stock->gained += amount; // all it was given, even what did not fit
    return taken;
}

} // namespace ge::mod::reserve
