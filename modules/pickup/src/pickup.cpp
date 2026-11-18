#include "ge/mod/pickup/pickup.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <vector>

namespace ge::mod::pickup {

namespace {

struct Candidate {
    ecs::Entity entity;
    float x = 0.0F;
    float y = 0.0F;
    float radius = 0.0F;
    int value = 0;
};

bool touching(float dx, float dy, float reach) {
    return dx * dx + dy * dy <= reach * reach;
}

} // namespace

void collect(ecs::World& world) {
    // Entities cannot be removed while iterating, so the pickups are gathered
    // first (in entity order, which is the same on every run), then matched.
    std::vector<Candidate> pickups;
    world.each<const Pickup, const transform2d::Position>(
        [&pickups](ecs::Entity entity, const Pickup& pickup,
                   const transform2d::Position& position) {
            pickups.push_back(
                Candidate{entity, position.x, position.y, pickup.radius, pickup.value});
        });
    if (pickups.empty()) {
        return;
    }
    auto* commands = world.resource<ecs::Commands>();
    std::vector<bool> taken(pickups.size(), false);
    world.each<Collector, const transform2d::Position>(
        [&](ecs::Entity, Collector& collector, const transform2d::Position& position) {
            for (std::size_t i = 0; i < pickups.size(); ++i) {
                const Candidate& pickup = pickups[i];
                if (!taken[i] && touching(position.x - pickup.x, position.y - pickup.y,
                                          collector.radius + pickup.radius)) {
                    taken[i] = true;
                    collector.score += pickup.value;
                    commands->despawn(pickup.entity);
                }
            }
        });
}

} // namespace ge::mod::pickup
