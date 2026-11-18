#pragma once

#include "ge/core/hash.hpp"

#include <cstdint>

namespace ge::app {

/// The size in pixels of the area the game draws in: the window of a player,
/// the same size when nothing is shown. An interface anchors itself to its
/// edges (`ui.Anchor`). It comes from the game's manifest (`window`), not from
/// the machine, so a game gives the same results everywhere.
struct Viewport {
    std::uint32_t width = 800;
    std::uint32_t height = 600;

    void hash(Hasher& hasher) const {
        hasher.add(width);
        hasher.add(height);
    }
};

} // namespace ge::app
