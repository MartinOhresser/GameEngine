#include "ge/app/host.hpp"

#include "ge/app/input.hpp"
#include "ge/app/lists.hpp"
#include "ge/app/runtime_lists.hpp"
#include "ge/app/scene.hpp"
#include "ge/mod/host/host.gen.hpp"

#include <cstddef>

namespace ge::mod::host {

namespace {

constexpr std::uint32_t none = 4294967295U;

bool pressed(const app::Actions& actions, const app::ActionNames& names, std::uint32_t action) {
    return action != none && action < names.names.size() && actions.pressed(names.names[action]);
}

/// The number a Call sends: its own, or the selected item of the list it names. `none` when it
/// names a list that has no selection yet (nothing is then sent).
std::uint32_t argument_of(const ecs::World& world, const Call& call) {
    if (call.list == none) {
        return call.argument;
    }
    const auto* lists = world.resource<app::RuntimeLists>();
    const auto* list_names = world.resource<app::ListNames>();
    if (lists == nullptr || list_names == nullptr) {
        return app::RuntimeLists::none;
    }
    const std::string_view list = list_names->name("runtime_list", call.list);
    return list.empty() ? app::RuntimeLists::none : lists->selected(list);
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
            if (item.command == none || !pressed(*actions, *names, item.action)) {
                return;
            }
            if (const std::uint32_t argument = argument_of(world, item);
                argument != app::RuntimeLists::none || item.list == none) {
                requests->pending.push_back(app::HostRequest{item.command, argument});
            }
        });
    }
}

} // namespace ge::mod::host
