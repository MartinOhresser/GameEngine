#pragma once

// The launcher's host: the part of the launcher that a game cannot do. The launcher
// itself is a game made of JSON files (`tools/launcher/`, the same `ui` and `text2d`
// as any game); this host reads the disk for it (the games), starts the game that was
// chosen on the same window, and brings the launcher back when that game is left.

#include "ge/game/runtime.hpp"
#include "ge/game/summary.hpp"
#include "ge/platform/backend.hpp"

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <span>
#include <string>
#include <vector>

namespace ge::platform::sdl {

struct LauncherOptions {
    /// The folder whose subfolders are the games.
    std::string games_dir = "games";
    /// Stop the launcher after this many frames of its own; 0 means until the player quits.
    std::uint64_t max_frames = 0;
    /// Where a game keeps its save data, from the game's name; no saving when empty.
    std::function<std::string(const std::string&)> save_path_of;
};

struct LauncherResult {
    bool started = false;
    /// The names of the games that were played, in order.
    std::vector<std::string> played;
};

/// The lines the launcher shows about a game (the run-time list "details"): title, version,
/// the description cut to the width of the panel, the window, the modules. Always the same
/// number of lines, so that the launcher's texts can be placed once.
inline constexpr std::size_t detail_line_count = 10;
[[nodiscard]] std::vector<std::string> detail_lines(const game::GameSummary& summary);

/// Runs the launcher on `backend` until the player quits it (its Quit button, Escape or
/// the window). The games are listed in the run-time list "games" and the selected one's
/// lines in "details"; a game's "play" command starts that game, whatever the launcher
/// was doing is paused, and the launcher comes back, with its selection, when the game
/// ends (its own Quit button, Escape) or until the window is closed.
LauncherResult run_launcher(Backend& backend, const game::LoadedGame& launcher,
                            const std::string& launcher_manifest,
                            std::span<const game::ModuleEntry> modules,
                            const LauncherOptions& options, std::ostream& err);

/// gelauncher: the launcher in a window.
///
///     gelauncher [--games DIR] [--launcher GAME.json] [--headless] [--frames N]
///                [--screenshot FILE.bmp]
///
/// Exit codes: 0 success, 1 invalid launcher or a failure to start, 2 usage.
int run_gelauncher(const std::vector<std::string>& args, std::span<const game::ModuleEntry> modules,
                   std::ostream& out, std::ostream& err);

} // namespace ge::platform::sdl
