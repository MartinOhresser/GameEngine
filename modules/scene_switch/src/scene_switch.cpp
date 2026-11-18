#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/mod/collision2d/collision2d.gen.hpp"
#include "ge/mod/scene_switch/scene_switch.gen.hpp"

#include <cstddef>

namespace ge::mod::scene_switch {

namespace {

bool action_pressed(const app::Actions* actions, const app::ActionNames* names,
                    const GoToScene& go) {
    if (!go.use_action || actions == nullptr || names == nullptr ||
        go.action >= names->names.size()) {
        return false;
    }
    return actions->pressed(names->names[go.action]);
}

bool touching(ecs::World& world, ecs::Entity entity, const GoToScene& go) {
    if (go.touch_layers == 0U) {
        return false;
    }
    const auto* contacts = world.get<collision2d::Contacts>(entity);
    return contacts != nullptr && (contacts->layers & go.touch_layers) != 0U;
}

} // namespace

void trigger(ecs::World& world) {
    auto* request = world.resource<app::SceneRequest>();
    const auto* actions = world.resource<app::Actions>();
    const auto* names = world.resource<app::ActionNames>();
    if (request == nullptr) {
        return;
    }
    world.each<GoToScene>([&](ecs::Entity entity, GoToScene& go) {
        ++go.elapsed;
        const bool timer = go.after_ticks != 0U && go.elapsed >= go.after_ticks;
        const bool fires =
            timer || action_pressed(actions, names, go) || touching(world, entity, go);
        if (fires && !request->scene) {
            request->scene = go.scene; // the first one in entity order wins the tick
        }
    });
}

} // namespace ge::mod::scene_switch
