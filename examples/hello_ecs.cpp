// Smallest useful program: a plugin that adds a component, a system and a
// resource, then runs a few frames.
//
// Run it with: ./build/dev/examples/hello_ecs

#include "ge/app/app.hpp"

#include <iostream>

namespace {

struct Position {
    float x;
    float y;
};

struct Velocity {
    float x;
    float y;
};

struct FrameCounter {
    unsigned frames = 0;
};

void count_frames(ge::ecs::World& world) {
    if (auto* counter = world.resource<FrameCounter>(); counter != nullptr) {
        ++counter->frames;
    }
}

void spawn_ship(ge::ecs::World& world) {
    world.spawn_with(Position{0.0F, 0.0F}, Velocity{1.0F, 0.5F});
}

void apply_velocity(ge::ecs::World& world) {
    world.each<Position, const Velocity>(
        [](ge::ecs::Entity, Position& position, const Velocity& velocity) {
            position.x += velocity.x;
            position.y += velocity.y;
        });
}

struct MovementPlugin {
    void build(ge::app::App& app) const {
        app.add_system(ge::app::Stage::Startup, spawn_ship, "spawn_ship");
        app.add_system(ge::app::Stage::Update, apply_velocity, "apply_velocity");
    }
};

} // namespace

int main() {
    ge::app::App app;
    app.insert_resource(FrameCounter{});
    app.add_system(ge::app::Stage::PreUpdate, count_frames, "count_frames");
    app.add_plugin(MovementPlugin{});

    app.run_frames(3);

    std::cout << "ran " << app.world().resource<FrameCounter>()->frames << " frames\n";
    app.world().each<const Position>([](ge::ecs::Entity entity, const Position& position) {
        std::cout << "entity " << entity.index() << " is at (" << position.x << ", " << position.y
                  << ")\n";
    });
    return 0;
}
