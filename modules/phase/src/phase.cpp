#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/ecs/commands.hpp"
#include "ge/mod/phase/phase.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <cstdint>
#include <optional>

namespace ge::mod::phase {

namespace {

/// The current phase: the one of the oldest Phase entity.
std::optional<std::uint32_t> current_phase(ecs::World& world) {
    std::optional<std::uint32_t> current;
    world.each<const Phase>([&current](ecs::Entity, const Phase& phase) {
        if (!current) {
            current = phase.current;
        }
    });
    return current;
}

bool in_set(std::uint32_t set, std::uint32_t phase) {
    return phase < 32U && ((set >> phase) & 1U) != 0U;
}

} // namespace

void transition(ecs::World& world) {
    const auto* names = world.resource<app::ActionNames>();
    const auto* actions = world.resource<app::Actions>();
    const std::optional<std::uint32_t> current = current_phase(world);
    if (names == nullptr || actions == nullptr || !current) {
        return;
    }
    std::optional<std::uint32_t> next;
    world.each<const Transition>([&](ecs::Entity, const Transition& change) {
        if (next || change.action >= names->names.size() ||
            !actions->pressed(names->names[change.action])) {
            return;
        }
        if (change.from_any || in_set(change.from, *current)) {
            next = change.to;
        }
    });
    if (!next) {
        return;
    }
    bool done = false;
    world.each<Phase>([&](ecs::Entity, Phase& phase) {
        if (!done) {
            phase.current = *next;
            done = true;
        }
    });
}

void apply(ecs::World& world) {
    const std::optional<std::uint32_t> current = current_phase(world);
    if (!current) {
        return;
    }
    auto* commands = world.resource<ecs::Commands>();
    world.each<const ShowIn>([&](ecs::Entity entity, const ShowIn& shown) {
        const bool hidden = world.has<transform2d::Hidden>(entity);
        const bool should_show = in_set(shown.phases, *current);
        if (should_show && hidden) {
            commands->remove<transform2d::Hidden>(entity);
        } else if (!should_show && !hidden) {
            commands->insert(entity, transform2d::Hidden{});
        }
    });
    world.each<const ActiveIn>([&](ecs::Entity entity, const ActiveIn& active) {
        const bool disabled = world.has<transform2d::Disabled>(entity);
        const bool should_work = in_set(active.phases, *current);
        if (should_work && disabled) {
            commands->remove<transform2d::Disabled>(entity);
        } else if (!should_work && !disabled) {
            commands->insert(entity, transform2d::Disabled{});
        }
    });
}

} // namespace ge::mod::phase
