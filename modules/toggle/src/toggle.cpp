#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/ecs/commands.hpp"
#include "ge/mod/tag/find.hpp"
#include "ge/mod/toggle/toggle.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <cstdint>
#include <string_view>

namespace ge::mod::toggle {

void flip(ecs::World& world) {
    const auto* actions = world.resource<app::Actions>();
    const auto* names = world.resource<app::ActionNames>();
    if (actions != nullptr && names != nullptr) {
        world.each<Toggle>([&](ecs::Entity, Toggle& toggle) {
            if (toggle.action >= names->names.size()) {
                return;
            }
            const std::string_view action = names->names[toggle.action];
            const bool was = toggle.on;
            toggle.on = toggle.hold ? actions->down(action)
                                    : (actions->pressed(action) ? !toggle.on : toggle.on);
            if (toggle.on && !was) {
                toggle.times += 1U;
            }
        });
    }
    const tag::Tagged tags(world);
    auto* commands = world.resource<ecs::Commands>();
    world.each<const ShownBy>([&](ecs::Entity entity, const ShownBy& shown) {
        const ecs::Entity holder = tags.find(shown.toggle);
        const auto* toggle = holder.is_null() ? nullptr : world.get<Toggle>(holder);
        const bool on = toggle != nullptr && toggle->on;
        const bool show = on != shown.inverse;
        const bool hidden = world.has<transform2d::Hidden>(entity);
        if (show && hidden) {
            commands->remove<transform2d::Hidden>(entity);
        } else if (!show && !hidden) {
            commands->insert(entity, transform2d::Hidden{});
        }
    });
}

} // namespace ge::mod::toggle
