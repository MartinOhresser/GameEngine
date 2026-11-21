#pragma once

// The program of an exported game: it plays the game found in the `data/` folder next to
// the executable, in a window, and nothing else. Each export links this with a registry of
// only the modules its game uses (see `ge_add_player` in platform/sdl/CMakeLists.txt).

#include "ge/game/runtime.hpp"

#include <iosfwd>
#include <span>
#include <string>
#include <vector>

namespace ge::platform::sdl {

/// The title of the window of an exported game: its manifest's `title`, else its name made
/// readable. Also what an error dialog is called when the game cannot start.
[[nodiscard]] std::string player_title(const game::LoadedGame& loaded);

/// Plays the game of `<program folder>/data/game.json`. Escape or the window's close button
/// leaves it. Options meant for tests and diagnostics, not for players:
///
///     --data DIR        the folder of the game (default: data next to the executable)
///     --headless        no window and no sound card
///     --software        draw with the processor, as a computer without a graphics card does
///     --frames N        stop after N frames
///     --screenshot F    save the last frame as a .bmp file
///
/// Exit codes: 0 success, 1 the game cannot start, 2 usage.
int run_player(const std::vector<std::string>& args, std::span<const game::ModuleEntry> modules,
               std::ostream& out, std::ostream& err);

} // namespace ge::platform::sdl
