#pragma once

#include "ge/app/render.hpp"
#include "ge/core/hash.hpp"
#include "ge/ecs/world.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace ge::mod::sprite2d {

/// Drawings that other modules want drawn among the sprites, each on a layer like a Sprite's
/// (particles behind a foreground, a trail under a character). A module adds them every frame
/// before `sprite2d.draw` (in Update, or in PostUpdate before it); `draw` sorts them with the
/// sprites, draws them and empties the list. On a layer, sprites come first.
///
/// Presentation only: it takes no part in the world fingerprint.
struct LayeredDraws {
    struct Item {
        std::int32_t layer = 0;
        app::DrawCommand command;
    };
    std::vector<Item> items;

    void add(std::int32_t layer, app::DrawCommand command) {
        items.push_back({layer, std::move(command)});
    }

    void hash(Hasher& /*hasher*/) const noexcept {}
};

/// The world's list of layered drawings, made on first use.
inline LayeredDraws& layered_draws(ecs::World& world) {
    if (auto* draws = world.resource<LayeredDraws>(); draws != nullptr) {
        return *draws;
    }
    return world.insert_resource(LayeredDraws{});
}

} // namespace ge::mod::sprite2d
