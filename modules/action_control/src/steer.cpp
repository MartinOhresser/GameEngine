#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/app/time.hpp"
#include "ge/mod/action_control/action_control.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace ge::mod::action_control {

namespace {

/// The value of the action an entity names, or of the default one when it names none.
float value_of(const app::Actions& actions, const app::ActionNames* names, std::uint32_t action,
               std::string_view fallback) {
    if (names != nullptr && action < names->names.size()) {
        return actions.value(names->names[action]);
    }
    return actions.value(fallback);
}

/// The speed along one axis after a tick of steering towards `wanted`: at once without an
/// acceleration, else by at most acceleration (braking when slowing down or turning) times `step`.
float steered(const Controlled& controlled, float now, float wanted, float step) {
    if (controlled.acceleration <= 0.0F) {
        return wanted;
    }
    const bool faster = std::abs(wanted) > std::abs(now) && wanted * now >= 0.0F;
    const float rate =
        !faster && controlled.braking > 0.0F ? controlled.braking : controlled.acceleration;
    const float most = rate * step;
    return now + std::clamp(wanted - now, -most, most);
}

} // namespace

void steer(ecs::World& world) {
    const auto* actions = world.resource<app::Actions>();
    if (actions == nullptr) {
        return;
    }
    const auto* names = world.resource<app::ActionNames>();
    const float step = world.resource<app::Time>()->fixed_delta *
                       world.resource<transform2d::Params>()->speed_scale;
    world.each<const Controlled, transform2d::Velocity>(
        [&](ecs::Entity entity, const Controlled& controlled, transform2d::Velocity& velocity) {
            // A Disabled entity is steered as if nothing were pressed: it slows to a stop.
            const float on = world.has<transform2d::Disabled>(entity) ? 0.0F : 1.0F;
            if (controlled.horizontal) {
                const float wanted = on * value_of(*actions, names, controlled.action_x, "move_x") *
                                     controlled.speed;
                velocity.x = steered(controlled, velocity.x, wanted, step);
            }
            if (controlled.vertical) {
                const float wanted = on * value_of(*actions, names, controlled.action_y, "move_y") *
                                     controlled.speed;
                velocity.y = steered(controlled, velocity.y, wanted, step);
            }
        });
}

} // namespace ge::mod::action_control
