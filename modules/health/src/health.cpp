#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/ecs/commands.hpp"
#include "ge/mod/health/health.gen.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ge::mod::health {

namespace {

void raise(app::Actions* actions, const app::ActionNames* names, std::uint32_t action) {
    if (actions != nullptr && names != nullptr &&
        static_cast<std::size_t>(action) < names->names.size()) {
        actions->raise(names->names[action]);
    }
}

} // namespace

void apply_damage(ecs::World& world) {
    auto* commands = world.resource<ecs::Commands>();
    auto* actions = world.resource<app::Actions>();
    const auto* names = world.resource<app::ActionNames>();
    std::vector<ecs::Entity> hurt;
    world.each<const Damage>(
        [&hurt](ecs::Entity entity, const Damage&) { hurt.push_back(entity); });
    for (const ecs::Entity entity : hurt) {
        const std::int32_t amount = world.get<Damage>(entity)->amount;
        commands->remove<Damage>(entity);
        auto* life = world.get<Health>(entity);
        if (life == nullptr || world.has<Dead>(entity)) {
            continue;
        }
        life->amount = std::min(life->amount - amount, life->max);
        if (life->amount <= 0) {
            life->amount = 0;
            commands->insert(entity, Dead{});
            raise(actions, names, life->on_zero);
        }
    }
}

void reap(ecs::World& world) {
    auto* commands = world.resource<ecs::Commands>();
    world.each<const Health, const Dead>(
        [commands](ecs::Entity entity, const Health& life, const Dead&) {
            if (life.despawn_on_zero) {
                commands->despawn(entity);
            }
        });
}

} // namespace ge::mod::health
