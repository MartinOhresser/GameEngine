#pragma once

#include <cstdint>
#include <vector>

namespace ge::app {

/// One entity to create from a prefab of the game while it runs.
struct SpawnRequest {
    /// The prefab: its index in the game's `prefab` list (a `"ref": "prefab"` field holds it).
    std::uint32_t prefab = 0;
    /// Where it appears (its `transform2d.Position`, when the game uses that module).
    float x = 0.0F;
    float y = 0.0F;
    /// Replaces the prefab's `transform2d.Velocity` when `has_velocity` is true.
    bool has_velocity = false;
    float vx = 0.0F;
    float vy = 0.0F;
};

/// Entities that systems want created from prefabs: a generator of platforms,
/// a gun, an explosion. The app creates them between two ticks through its
/// prefab spawner, so nothing appears in the middle of a tick; a scene change
/// drops the requests that were not served yet.
struct SpawnRequests {
    std::vector<SpawnRequest> queue;

    void spawn(std::uint32_t prefab, float x, float y) {
        queue.push_back(SpawnRequest{prefab, x, y, false, 0.0F, 0.0F});
    }

    void spawn_moving(std::uint32_t prefab, float x, float y, float vx, float vy) {
        queue.push_back(SpawnRequest{prefab, x, y, true, vx, vy});
    }
};

} // namespace ge::app
