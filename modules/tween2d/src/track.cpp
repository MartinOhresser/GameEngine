// Tracks: numbers played along keys, for cut scenes.

#include "ge/app/fields.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/mod/tag/find.hpp"
#include "ge/mod/tween2d/tween2d.gen.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace ge::mod::tween2d {

namespace {

constexpr std::uint32_t none = 0xFFFFFFFFU;
constexpr std::uint32_t most_keys = 6U;

struct Key {
    std::uint32_t tick = 0;
    float value = 0.0F;
};

std::array<Key, most_keys> keys_of(const Track& track) {
    return {Key{track.t0, track.v0}, Key{track.t1, track.v1}, Key{track.t2, track.v2},
            Key{track.t3, track.v3}, Key{track.t4, track.v4}, Key{track.t5, track.v5}};
}

/// How fast the curve goes through a key (Catmull-Rom): as from its key before to its key after;
/// still at the first and the last.
float slope_at(const std::array<Key, most_keys>& keys, std::uint32_t count, std::uint32_t index) {
    if (index == 0U || index + 1U >= count) {
        return 0.0F;
    }
    const Key& before = keys[index - 1U];
    const Key& after = keys[index + 1U];
    return (after.value - before.value) /
           static_cast<float>(std::max(after.tick - before.tick, 1U));
}

/// The number between two keys of a curve: a cubic through both, with the slopes of the keys.
float curved(const std::array<Key, most_keys>& keys, std::uint32_t count, std::uint32_t index,
             float t) {
    const Key& from = keys[index - 1U];
    const Key& to = keys[index];
    const auto span = static_cast<float>(std::max(to.tick - from.tick, 1U));
    const float t2 = t * t;
    const float t3 = t2 * t;
    return ((2.0F * t3) - (3.0F * t2) + 1.0F) * from.value +
           (t3 - (2.0F * t2) + t) * span * slope_at(keys, count, index - 1U) +
           ((-2.0F * t3) + (3.0F * t2)) * to.value +
           (t3 - t2) * span * slope_at(keys, count, index);
}

/// The number at a tick of the track; false before its first key.
bool value_at(const Track& track, std::uint32_t tick, float& value) {
    const std::array<Key, most_keys> keys = keys_of(track);
    const std::uint32_t count = std::clamp(track.keys, 1U, most_keys);
    if (tick < keys[0].tick) {
        return false;
    }
    for (std::uint32_t index = 1; index < count; index += 1) {
        const Key& from = keys[index - 1];
        const Key& to = keys[index];
        if (tick < to.tick) {
            float t = static_cast<float>(tick - from.tick) /
                      static_cast<float>(std::max(to.tick - from.tick, 1U));
            if (track.curve) {
                value = curved(keys, count, index, t);
                return true;
            }
            if (track.smooth) {
                t = t * t * (3.0F - 2.0F * t);
            }
            value = from.value + (to.value - from.value) * t;
            return true;
        }
    }
    value = keys[count - 1].value;
    return true;
}

bool pressed(const ecs::World& world, std::uint32_t action) {
    const auto* actions = world.resource<app::Actions>();
    const auto* names = world.resource<app::ActionNames>();
    return actions != nullptr && names != nullptr && action < names->names.size() &&
           actions->pressed(names->names[action]);
}

void raise(ecs::World& world, std::uint32_t action) {
    auto* actions = world.resource<app::Actions>();
    const auto* names = world.resource<app::ActionNames>();
    if (actions != nullptr && names != nullptr && action < names->names.size()) {
        actions->raise(names->names[action]);
    }
}

} // namespace

void play(ecs::World& world) {
    const auto* table = world.resource<app::FieldTable>();
    const tag::Tagged tags(world);
    world.each<Track>([&](ecs::Entity entity, Track& track) {
        if (!track.started) {
            track.started = track.start == none || pressed(world, track.start);
            if (!track.started) {
                return;
            }
        } else if (!track.done) {
            track.elapsed += 1U;
        }
        const ecs::Entity target = track.onto == none ? entity : tags.find(track.onto);
        float value = 0.0F;
        if (table != nullptr && track.field < table->entries.size() && world.is_alive(target) &&
            value_at(track, track.elapsed, value)) {
            const app::FieldEntry& entry = table->entries[track.field];
            if (entry.component != nullptr && entry.component->write != nullptr) {
                static_cast<void>(entry.component->write(world, target, entry.field,
                                                         app::Scalar{static_cast<double>(value)}));
            }
        }
        const std::uint32_t last = keys_of(track)[std::clamp(track.keys, 1U, most_keys) - 1].tick;
        if (track.loop && track.elapsed >= last) {
            track.elapsed = track.t0;
        } else if (!track.done && track.elapsed >= last) {
            track.done = true;
            raise(world, track.end_action);
        }
    });
}

} // namespace ge::mod::tween2d
