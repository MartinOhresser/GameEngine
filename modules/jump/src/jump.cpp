#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/app/time.hpp"
#include "ge/mod/collision2d/collision2d.gen.hpp"
#include "ge/mod/jump/jump.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace ge::mod::jump {

namespace {

/// Timers stop growing here: long past any grace time, and far from where a float loses
/// its precision.
constexpr float long_ago = 1000.0F;

/// The name of the action an entity names, or "jump" when it names none.
std::string_view action_name(const app::ActionNames* names, std::uint32_t action) {
    if (names != nullptr && action < names->names.size()) {
        return names->names[action];
    }
    return "jump";
}

/// Whether a jump may start now, and spends what it uses (the grace time, or an air jump).
bool allowed(Jumper& jumper) {
    if (jumper.since_ground <= jumper.coyote_time) {
        jumper.since_ground = long_ago; // no second jump from the same ground
        return true;
    }
    if (jumper.air_jumps_used < jumper.air_jumps) {
        jumper.air_jumps_used += 1U;
        return true;
    }
    return false;
}

void launch_one(Jumper& jumper, transform2d::Velocity& velocity, bool held) {
    velocity.y = -jumper.speed;
    jumper.since_press = long_ago;
    jumper.jumps += 1U;
    jumper.holding = held;
    if (!held) {
        // Pressed and let go before the landing: the short jump.
        velocity.y *= jumper.cut;
    }
}

} // namespace

void launch(ecs::World& world) {
    const auto* actions = world.resource<app::Actions>();
    if (actions == nullptr) {
        return;
    }
    const auto* names = world.resource<app::ActionNames>();
    const float step = world.resource<app::Time>()->fixed_delta *
                       world.resource<transform2d::Params>()->speed_scale;
    world.each<Jumper, transform2d::Velocity>([&](ecs::Entity entity, Jumper& jumper,
                                                  transform2d::Velocity& velocity) {
        const std::string_view action = action_name(names, jumper.action);
        const auto* blocked = world.get<collision2d::Blocked>(entity);
        if (blocked != nullptr && blocked->below) {
            jumper.since_ground = 0.0F;
            jumper.air_jumps_used = 0U;
        } else {
            jumper.since_ground = std::min(jumper.since_ground + step, long_ago);
        }
        // A Disabled jumper does not hear its action: it neither jumps nor holds a jump.
        const bool on = !world.has<transform2d::Disabled>(entity);
        jumper.since_press =
            on && actions->pressed(action) ? 0.0F : std::min(jumper.since_press + step, long_ago);

        const bool held = on && actions->down(action);
        if (jumper.holding && (!held || velocity.y >= 0.0F)) {
            jumper.holding = false;
            if (velocity.y < 0.0F) {
                velocity.y *= jumper.cut;
            }
        }
        if (jumper.since_press <= jumper.buffer_time && allowed(jumper)) {
            launch_one(jumper, velocity, held);
        }
    });
}

} // namespace ge::mod::jump
