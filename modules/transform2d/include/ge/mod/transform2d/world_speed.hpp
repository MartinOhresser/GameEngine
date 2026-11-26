#pragma once

// How fast the world moves: the game's speed_scale (a parameter of transform2d) times the factor
// of every WorldSpeed (a slow motion played by the game). Header only, so that the modules that
// move things by the tick (gravity, particles, animations) read it without a call into another
// module.

#include "ge/ecs/world.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

namespace ge::mod::transform2d {

/// The share of a tick the world moves by this tick: 1 normally, 0.25 in a slow motion.
[[nodiscard]] inline float world_speed(ecs::World& world) {
    const auto* params = world.resource<Params>();
    float speed = params == nullptr ? 1.0F : params->speed_scale;
    world.each<const WorldSpeed>(
        [&speed](ecs::Entity /*entity*/, const WorldSpeed& slow) { speed *= slow.factor; });
    return speed;
}

} // namespace ge::mod::transform2d
