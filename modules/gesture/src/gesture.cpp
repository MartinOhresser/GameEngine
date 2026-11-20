#include "ge/app/direction.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/mod/gesture/gesture.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <cmath>
#include <cstdint>

namespace ge::mod::gesture {

namespace {

float value_of(const app::Actions& actions, const app::ActionNames& names, std::uint32_t action) {
    return action < names.names.size() ? actions.value(names.names[action]) : 0.0F;
}

/// The action of the direction the movement (dx, dy) went in.
std::uint32_t action_of(const Swipe& swipe, float dx, float dy) {
    const app::Direction direction = app::Direction::of(dx, dy);
    if (direction.vertical) {
        return direction.sign < 0.0F ? swipe.up : swipe.down;
    }
    return direction.sign < 0.0F ? swipe.left : swipe.right;
}

/// Whether a movement lasting `elapsed` ticks counts as a swipe.
bool is_swipe(const Swipe& swipe, float dx, float dy) {
    const bool far_enough = std::hypot(dx, dy) >= swipe.min_distance;
    const bool quick_enough = swipe.max_ticks == 0U || swipe.elapsed <= swipe.max_ticks;
    return far_enough && quick_enough;
}

bool is_raised(const app::Actions& actions, const app::ActionNames& names, std::uint32_t action,
               bool released) {
    if (action >= names.names.size()) {
        return false;
    }
    return released ? actions.released(names.names[action]) : actions.pressed(names.names[action]);
}

} // namespace

void detect(ecs::World& world) {
    auto* actions = world.resource<app::Actions>();
    const auto* names = world.resource<app::ActionNames>();
    if (actions == nullptr || names == nullptr) {
        return;
    }
    world.each<Swipe>([&](ecs::Entity entity, Swipe& swipe) {
        if (world.has<transform2d::Disabled>(entity)) {
            swipe.active = false;
            return;
        }
        const float x = value_of(*actions, *names, swipe.pointer_x);
        const float y = value_of(*actions, *names, swipe.pointer_y);
        if (is_raised(*actions, *names, swipe.press, false)) {
            swipe = Swipe{swipe.pointer_x,
                          swipe.pointer_y,
                          swipe.press,
                          swipe.min_distance,
                          swipe.max_ticks,
                          swipe.left,
                          swipe.right,
                          swipe.up,
                          swipe.down,
                          true,
                          x,
                          y,
                          0U};
            return;
        }
        if (!swipe.active) {
            return;
        }
        ++swipe.elapsed;
        if (!is_raised(*actions, *names, swipe.press, true)) {
            return;
        }
        swipe.active = false;
        const float dx = x - swipe.start_x;
        const float dy = y - swipe.start_y;
        const std::uint32_t action = action_of(swipe, dx, dy);
        if (is_swipe(swipe, dx, dy) && action < names->names.size()) {
            actions->raise(names->names[action]);
        }
    });
}

} // namespace ge::mod::gesture
