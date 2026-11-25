#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/app/time.hpp"
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
    auto* time = world.resource<app::Time>();
    const bool was_paused = time != nullptr && time->paused;
    bool pausing = false;
    if (actions != nullptr && names != nullptr) {
        const auto pressed = [&](std::uint32_t action) {
            return action < names->names.size() && actions->pressed(names->names[action]);
        };
        world.each<Toggle>([&](ecs::Entity, Toggle& toggle) {
            // While paused, only what pauses listens: the rest of the game is stopped.
            if (toggle.action < names->names.size() && (toggle.pauses || !was_paused)) {
                const std::string_view action = names->names[toggle.action];
                const bool was = toggle.on;
                toggle.on = toggle.hold ? actions->down(action)
                                        : (actions->pressed(action) ? !toggle.on : toggle.on);
                if (toggle.on && !was) {
                    toggle.times += 1U;
                }
            }
            if ((toggle.pauses || !was_paused) && pressed(toggle.off)) {
                toggle.on = false;
            }
        });
    }
    world.each<const Toggle>([&](ecs::Entity, const Toggle& toggle) {
        pausing = pausing || (toggle.pauses && toggle.on);
    });
    if (time != nullptr) {
        time->paused = pausing;
    }
    const tag::Tagged tags(world);
    auto* commands = world.resource<ecs::Commands>();
    const auto on_of = [&](std::uint32_t name) {
        const ecs::Entity holder = tags.find(name);
        const auto* toggle = holder.is_null() ? nullptr : world.get<Toggle>(holder);
        return toggle != nullptr && toggle->on;
    };
    world.each<const ShownBy>([&](ecs::Entity entity, const ShownBy& shown) {
        const bool show = on_of(shown.toggle) != shown.inverse;
        const bool hidden = world.has<transform2d::Hidden>(entity);
        if (show && hidden) {
            commands->remove<transform2d::Hidden>(entity);
        } else if (!show && !hidden) {
            commands->insert(entity, transform2d::Hidden{});
        }
    });
    world.each<const ActiveBy>([&](ecs::Entity entity, const ActiveBy& active) {
        const bool work = on_of(active.toggle) != active.inverse;
        const bool disabled = world.has<transform2d::Disabled>(entity);
        if (work && disabled) {
            commands->remove<transform2d::Disabled>(entity);
        } else if (!work && !disabled) {
            commands->insert(entity, transform2d::Disabled{});
        }
    });
}

} // namespace ge::mod::toggle
