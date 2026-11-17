#include "ge/ecs/commands.hpp"

namespace ge::ecs {

void Commands::apply(World& world) {
    while (!queue_.empty()) {
        // Commands run from a private copy of the queue, so one that queues more
        // commands appends to a fresh queue instead of invalidating this loop.
        std::vector<std::unique_ptr<Concept>> batch;
        batch.swap(queue_);
        for (const auto& command : batch) {
            command->run(world);
        }
    }
}

} // namespace ge::ecs
