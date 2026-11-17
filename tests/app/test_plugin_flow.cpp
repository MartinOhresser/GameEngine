// End-to-end check of the extension story: a plugin brings its own components
// and systems, and the app runs them without the core knowing about them.

#include "ge/app/app.hpp"

#include "../support/mini_test.hpp"

#include <vector>

using ge::app::App;
using ge::app::Stage;
using ge::ecs::Entity;
using ge::ecs::World;

namespace {

struct Health {
    int points;
};

struct Poison {
    int damage;
};

struct Deaths {
    std::vector<Entity> entities;
};

void apply_poison(World& world) {
    world.each<Health, const Poison>(
        [](Entity, Health& health, const Poison& poison) { health.points -= poison.damage; });
}

void reap_the_dead(World& world) {
    std::vector<Entity> dead;
    world.each<const Health>([&dead](Entity entity, const Health& health) {
        if (health.points <= 0) {
            dead.push_back(entity);
        }
    });
    for (const Entity entity : dead) {
        static_cast<void>(world.despawn(entity));
        world.resource<Deaths>()->entities.push_back(entity);
    }
}

struct PoisonPlugin {
    void build(App& app) const {
        app.insert_resource(Deaths{});
        app.add_system(Stage::Update, apply_poison, "apply_poison");
        app.add_system(Stage::PostUpdate, reap_the_dead, "reap_the_dead");
    }
};

} // namespace

TEST_CASE("plugin flow: a plugin adds components and systems without touching the core") {
    App app;
    app.add_plugin(PoisonPlugin{});

    const Entity weak = app.world().spawn_with(Health{3}, Poison{2});
    const Entity sturdy = app.world().spawn_with(Health{10}, Poison{2});
    const Entity healthy = app.world().spawn_with(Health{1});

    app.run_frames(2);

    const World& world = app.world();
    CHECK(!world.is_alive(weak)); // 3 - 2 - 2 <= 0
    REQUIRE(world.get<Health>(sturdy) != nullptr);
    CHECK(world.get<Health>(sturdy)->points == 6);
    REQUIRE(world.get<Health>(healthy) != nullptr);
    CHECK(world.get<Health>(healthy)->points == 1);

    const auto* deaths = world.resource<Deaths>();
    REQUIRE(deaths != nullptr);
    REQUIRE(deaths->entities.size() == 1);
    CHECK(deaths->entities[0] == weak);
}
