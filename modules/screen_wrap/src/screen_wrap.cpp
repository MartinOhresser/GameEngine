#include "ge/mod/screen_wrap/screen_wrap.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <cmath>

namespace ge::mod::screen_wrap {

namespace {

/// Brings `value` into [0, size). A size of zero or less leaves it alone.
float wrapped(float value, float size) {
    if (size <= 0.0F) {
        return value;
    }
    float result = std::fmod(value, size);
    if (result < 0.0F) {
        result += size;
    }
    return result;
}

} // namespace

void wrap(ecs::World& world) {
    world.each<transform2d::Position, const WrapArea>(
        [](ecs::Entity, transform2d::Position& position, const WrapArea& area) {
            position.x = wrapped(position.x, area.width);
            position.y = wrapped(position.y, area.height);
        });
}

} // namespace ge::mod::screen_wrap
