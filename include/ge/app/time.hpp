#pragma once

#include "ge/core/hash.hpp"

#include <cstdint>

namespace ge::app {

/// Clock state, stored in the world as a resource by `App`.
///
/// Gameplay reads `fixed_delta` and `tick`, which are the same on every run.
/// `frame_delta` and `alpha` depend on the real clock: they are for
/// presentation (smooth movement, animation) and must not influence the
/// simulation.
struct Time {
    /// Fixed ticks completed so far.
    std::uint64_t tick = 0;
    /// Fixed ticks per second. Change it with `App::set_tick_rate`.
    std::uint32_t tick_rate = 60;
    /// Seconds simulated by one fixed tick.
    float fixed_delta = 1.0F / 60.0F;
    /// Real seconds that passed since the previous frame.
    float frame_delta = 0.0F;
    /// How far the next tick is, in [0, 1): for interpolating what is drawn.
    float alpha = 0.0F;
    /// Set by a host that draws in real time (`platform::run_loop`): the drawing then places
    /// things between the last two ticks by `alpha`, so that they move smoothly on any screen.
    /// Off in tests and scenarios, which see exactly the last tick.
    bool interpolate = false;

    /// Hashes only what the simulation depends on: `frame_delta`, `alpha` and
    /// `interpolate` follow the real clock or the host, so they are left out.
    void hash(Hasher& hasher) const {
        hasher.add(tick);
        hasher.add(tick_rate);
        hasher.add(fixed_delta);
    }
};

} // namespace ge::app
