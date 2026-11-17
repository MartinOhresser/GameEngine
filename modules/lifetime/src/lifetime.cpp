#include "ge/mod/lifetime/lifetime.gen.hpp"

namespace ge::mod::lifetime {

void expire(ecs::World& world) {
    auto* commands = world.resource<ecs::Commands>();
    world.each<Lifetime>([commands](ecs::Entity entity, Lifetime& life) {
        --life.ticks;
        if (life.ticks <= 0) {
            commands->despawn(entity);
        }
    });
}

} // namespace ge::mod::lifetime
