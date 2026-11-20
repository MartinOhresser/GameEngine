#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/app/spawn.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"
#include "ge/mod/waves/waves.gen.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ge::mod::waves {

namespace {

/// Ticks to wait, after the last thing of a wave is created, before looking whether any is left:
/// a request is served at the end of the tick, and a thing exists from the next one.
constexpr std::uint32_t settle_ticks = 3;

struct Output {
    app::Actions* actions = nullptr;
    const app::ActionNames* names = nullptr;

    void raise(std::uint32_t action) const {
        if (actions != nullptr && names != nullptr &&
            static_cast<std::size_t>(action) < names->names.size()) {
            actions->raise(names->names[action]);
        }
    }
    [[nodiscard]] bool pressed(std::uint32_t action) const {
        return actions != nullptr && names != nullptr &&
               static_cast<std::size_t>(action) < names->names.size() &&
               actions->pressed(names->names[action]);
    }
};

std::vector<ecs::Entity> groups_of(ecs::World& world, std::uint32_t wave) {
    std::vector<ecs::Entity> groups;
    world.each<const WaveGroup>([&](ecs::Entity entity, const WaveGroup& group) {
        if (group.wave == wave) {
            groups.push_back(entity);
        }
    });
    return groups;
}

std::uint32_t last_wave(ecs::World& world) {
    std::uint32_t last = 0;
    world.each<const WaveGroup>(
        [&last](ecs::Entity, const WaveGroup& group) { last = std::max(last, group.wave); });
    return last;
}

void start(ecs::World& world, WaveController& controller, const Output& out) {
    ++controller.current;
    controller.running = true;
    controller.elapsed = 0;
    controller.quiet = 0;
    for (const ecs::Entity entity : groups_of(world, controller.current)) {
        world.get<WaveGroup>(entity)->spawned = 0;
    }
    out.raise(controller.started_action);
}

/// Creates what is due; true when every group of the wave has created everything.
bool spawn_due(ecs::World& world, const WaveController& controller,
               const std::vector<ecs::Entity>& groups) {
    auto* requests = world.resource<app::SpawnRequests>();
    bool all_done = true;
    for (const ecs::Entity entity : groups) {
        auto* group = world.get<WaveGroup>(entity);
        while (group->spawned < group->count &&
               controller.elapsed >= group->delay_ticks + group->spawned * group->interval_ticks) {
            if (requests != nullptr) {
                requests->spawn_owned(group->prefab, controller.spawn_x, controller.spawn_y,
                                      entity.to_link());
            }
            ++group->spawned;
        }
        all_done = all_done && group->spawned >= group->count;
    }
    return all_done;
}

bool any_alive(ecs::World& world, const std::vector<ecs::Entity>& groups) {
    bool alive = false;
    world.each<const transform2d::Owner>([&](ecs::Entity, const transform2d::Owner& owner) {
        for (const ecs::Entity group : groups) {
            alive = alive || owner.entity == group.to_link();
        }
    });
    return alive;
}

void step(ecs::World& world, ecs::Entity entity, const Output& out) {
    auto* controller = world.get<WaveController>(entity);
    if (controller == nullptr || world.has<transform2d::Disabled>(entity)) {
        return;
    }
    controller->last = last_wave(world);
    if (!controller->running) {
        bool rested = false;
        if (controller->rest > 0U) {
            --controller->rest;
            rested = controller->rest == 0U;
        }
        if (controller->finished || controller->current >= controller->last) {
            return;
        }
        if (out.pressed(controller->start_action) || (controller->auto_next && rested)) {
            start(world, *controller, out);
        }
        return;
    }
    ++controller->elapsed;
    const std::vector<ecs::Entity> groups = groups_of(world, controller->current);
    if (!spawn_due(world, *controller, groups)) {
        return;
    }
    ++controller->quiet;
    if (controller->quiet < settle_ticks || any_alive(world, groups)) {
        return;
    }
    controller->running = false;
    out.raise(controller->cleared_action);
    if (controller->current >= controller->last) {
        controller->finished = true;
        out.raise(controller->all_cleared_action);
    } else {
        controller->rest = controller->auto_next ? std::max(controller->rest_ticks, 1U) : 0U;
    }
}

} // namespace

void run(ecs::World& world) {
    const Output out{world.resource<app::Actions>(), world.resource<app::ActionNames>()};
    std::vector<ecs::Entity> controllers;
    world.each<const WaveController>([&controllers](ecs::Entity entity, const WaveController&) {
        controllers.push_back(entity);
    });
    for (const ecs::Entity entity : controllers) {
        step(world, entity, out);
    }
}

} // namespace ge::mod::waves
