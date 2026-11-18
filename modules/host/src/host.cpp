#include "ge/app/host.hpp"

#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/mod/host/host.gen.hpp"

#include <cstddef>

namespace ge::mod::host {

namespace {

constexpr std::uint32_t none = 4294967295U;

bool pressed(const app::Actions& actions, const app::ActionNames& names, std::uint32_t action) {
    return action != none && action < names.names.size() && actions.pressed(names.names[action]);
}

} // namespace

void trigger(ecs::World& world) {
    const auto* actions = world.resource<app::Actions>();
    const auto* names = world.resource<app::ActionNames>();
    if (actions == nullptr || names == nullptr) {
        return;
    }
    const auto* info = world.resource<app::HostInfo>();
    const bool mobile = info != nullptr && info->mobile;
    if (auto* quit = world.resource<app::QuitRequest>(); quit != nullptr) {
        world.each<Quit>([&](ecs::Entity, Quit& item) {
            if ((!mobile || item.on_mobile) && pressed(*actions, *names, item.action)) {
                quit->requested = true;
            }
        });
    }
    if (auto* requests = world.resource<app::HostRequests>(); requests != nullptr) {
        world.each<Call>([&](ecs::Entity, Call& item) {
            if (item.command != none && pressed(*actions, *names, item.action)) {
                requests->pending.push_back(app::HostRequest{item.command, item.argument});
            }
        });
    }
}

} // namespace ge::mod::host
