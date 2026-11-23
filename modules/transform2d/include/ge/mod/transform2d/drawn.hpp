#pragma once

// Where to draw an entity between two ticks. The simulation moves in fixed ticks, while the screen
// may refresh at any rate (60, 120, 144 times a second): drawing the last tick as it is makes
// things move in small jumps. A host that draws in real time sets `app::Time::interpolate`, and
// the drawing systems then place each entity between where it was one tick before and where it is
// now, by `app::Time::alpha`. Presentation only: the simulation never reads it. Header only, so
// that a drawing module that depends on transform2d uses it without a call into another module.

#include "ge/app/time.hpp"
#include "ge/core/hash.hpp"
#include "ge/ecs/entity.hpp"
#include "ge/ecs/world.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

namespace ge::mod::transform2d {

/// Where every entity was at a tick, by slot index (the generation tells a reused slot apart).
struct PositionsAt {
    struct Entry {
        std::uint32_t generation = 0;
        bool present = false;
        Position position;
    };
    std::uint64_t tick = 0;
    bool taken = false;
    std::vector<Entry> entries;

    /// The position the entity had then, or null if it did not exist (or had no Position).
    [[nodiscard]] const Position* find(ecs::Entity entity) const noexcept {
        if (!taken || entity.index() >= entries.size()) {
            return nullptr;
        }
        const Entry& entry = entries[entity.index()];
        return entry.present && entry.generation == entity.generation() ? &entry.position : nullptr;
    }
};

/// The positions of the last two ticks the drawing saw, kept by the `remember` system. A resource
/// of the world that takes no part in its fingerprint (it is presentation, not simulation).
struct Trail {
    PositionsAt latest;
    PositionsAt older;
    /// A move longer than this in one tick is a jump (a teleport, a wrap around the screen, a new
    /// level): it is drawn at once, not slid across the screen.
    float snap_distance = 0.0F;

    void hash(Hasher& /*hasher*/) const noexcept {}

    /// The positions of the tick before `tick`: the latest ones when a tick ran since they were
    /// taken, the older ones otherwise.
    [[nodiscard]] const PositionsAt& before(std::uint64_t tick) const noexcept {
        return latest.taken && latest.tick < tick ? latest : older;
    }
};

/// Where to draw `entity`, whose Position is `position` now: the Position itself, unless the host
/// draws in real time (`app::Time::interpolate`), in which case it is the point between the
/// previous tick's position and this one, `app::Time::alpha` of the way.
[[nodiscard]] inline Position drawn_position(const ecs::World& world, ecs::Entity entity,
                                             const Position& position) {
    const auto* time = world.resource<app::Time>();
    const auto* trail = world.resource<Trail>();
    if (time == nullptr || trail == nullptr || !time->interpolate) {
        return position;
    }
    const PositionsAt& before = trail->before(time->tick);
    if (before.tick + 1U != time->tick) {
        return position; // several ticks ran at once (a slow frame): no single previous point
    }
    const Position* previous = before.find(entity);
    if (previous == nullptr) {
        return position; // new since then
    }
    const float dx = position.x - previous->x;
    const float dy = position.y - previous->y;
    if (trail->snap_distance > 0.0F && std::hypot(dx, dy) > trail->snap_distance) {
        return position;
    }
    const float alpha = time->alpha;
    return Position{previous->x + dx * alpha, previous->y + dy * alpha};
}

} // namespace ge::mod::transform2d
