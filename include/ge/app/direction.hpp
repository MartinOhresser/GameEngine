#pragma once

#include <cmath>

namespace ge::app {

/// One of the four directions of the screen, read from a pair of numbers (x, y) the way the
/// modules take one: the axis with the bigger number counts, and its sign says which way (y grows
/// downwards, so (0, 1) is down and (0, -1) is up). (0, 0) is down. A game writes a direction as
/// two numbers on a component (`fall_x`, `fall_y`) instead of a name, so that it also fits
/// the gravity of a world drawn the other way up.
struct Direction {
    /// True when the direction runs along y (up or down), false along x (left or right).
    bool vertical = true;
    /// +1 towards bigger coordinates (down, right), -1 towards smaller ones (up, left).
    float sign = 1.0F;

    static Direction of(float x, float y) {
        Direction direction;
        if (std::fabs(x) > std::fabs(y)) {
            direction.vertical = false;
            direction.sign = x < 0.0F ? -1.0F : 1.0F;
        } else {
            direction.sign = y < 0.0F ? -1.0F : 1.0F;
        }
        return direction;
    }

    /// The coordinate along the direction, so that "further" is bigger: `sign` times x or y.
    [[nodiscard]] float along(float x, float y) const { return sign * (vertical ? y : x); }

    /// The coordinate across the direction (not flipped).
    [[nodiscard]] float across(float x, float y) const { return vertical ? x : y; }

    /// A speed along the direction as the (x, y) velocity it makes.
    [[nodiscard]] float vx(float speed) const { return vertical ? 0.0F : sign * speed; }
    [[nodiscard]] float vy(float speed) const { return vertical ? sign * speed : 0.0F; }
};

} // namespace ge::app
