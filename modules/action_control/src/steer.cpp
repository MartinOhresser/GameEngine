#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/mod/action_control/action_control.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

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

} // namespace

void steer(ecs::World& world) {
    const auto* actions = world.resource<app::Actions>();
    if (actions == nullptr) {
        return;
    }
    const auto* names = world.resource<app::ActionNames>();
    world.each<const Controlled, transform2d::Velocity>(
        [&](ecs::Entity, const Controlled& controlled, transform2d::Velocity& velocity) {
            if (controlled.horizontal) {
                velocity.x =
                    value_of(*actions, names, controlled.action_x, "move_x") * controlled.speed;
            }
            if (controlled.vertical) {
                velocity.y =
                    value_of(*actions, names, controlled.action_y, "move_y") * controlled.speed;
            }
        });
}

} // namespace ge::mod::action_control
