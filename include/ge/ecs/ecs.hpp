#pragma once

/// Umbrella header of the ECS core.
///
///     #include "ge/ecs/ecs.hpp"
///
///     struct Position { float x, y; };
///     struct Velocity { float x, y; };
///
///     ge::ecs::World world;
///     world.spawn_with(Position{0, 0}, Velocity{1, 0});
///     world.each<Position, const Velocity>(
///         [](ge::ecs::Entity, Position& p, const Velocity& v) { p.x += v.x; });

#include "ge/ecs/entity.hpp" // IWYU pragma: export
#include "ge/ecs/world.hpp"  // IWYU pragma: export
