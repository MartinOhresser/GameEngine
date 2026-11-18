#pragma once

#include "ge/game/runtime.hpp"

#include <iosfwd>
#include <span>
#include <string>
#include <vector>

namespace ge::platform::sdl {

/// geplay: plays a game described by JSON files in a window.
///
///     geplay GAME.json [--frames N] [--size WxH] [--headless] [--screenshot FILE.bmp]
///
/// The window closes when the player closes it, or after N frames. `--headless`
/// uses no window and no sound card (the frames are drawn into memory), and
/// `--screenshot` writes the last frame as a .bmp file. Exit codes: 0 success,
/// 1 invalid game or a failure to start, 2 usage.
int run_geplay(const std::vector<std::string>& args, std::span<const game::ModuleEntry> modules,
               std::ostream& out, std::ostream& err);

} // namespace ge::platform::sdl
