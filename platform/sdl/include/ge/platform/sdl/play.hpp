#pragma once

// Playing one game on a backend that already has a window: what `geplay` does for
// the game it was given, and what the launcher does for the game that was chosen.

#include "ge/game/runtime.hpp"
#include "ge/platform/backend.hpp"

#include <cstdint>
#include <iosfwd>
#include <span>
#include <string>
#include <vector>

namespace ge::platform::sdl {

struct PlayOptions {
    /// Stop after this many frames; 0 means until the player leaves.
    std::uint64_t max_frames = 0;
    /// A window size other than the one of the manifest's `window`; 0 keeps it.
    int width = 0;
    int height = 0;
    /// The file the game keeps what must last between two runs in (high scores);
    /// nothing is read or written when empty.
    std::string save_path;
    /// Raw sources that leave the game for the host (the way back to a launcher).
    std::vector<std::string> back_sources = {"key:escape"};
};

struct PlayResult {
    /// False when the game could not start (problems were written to `err`).
    bool started = false;
    LoopResult loop;
    std::uint64_t ticks = 0;
};

/// The size of the window a game asks for, `options` first.
[[nodiscard]] std::pair<int, int> window_size_of(const game::LoadedGame& loaded,
                                                 const PlayOptions& options);

/// Starts `loaded` (a valid game read from `manifest_path`) on `backend` and plays it
/// until the player leaves, closes the window, or `max_frames` is reached. It starts
/// the backend's assets afresh, gives the window the game's size, loads the sounds
/// (the .wav files of the game's `sounds/` directory) and the atlases, and keeps the
/// save data. It does not restore the window afterwards: that is the host's business.
PlayResult play_loaded(Backend& backend, const game::LoadedGame& loaded,
                       const std::string& manifest_path, std::span<const game::ModuleEntry> modules,
                       const PlayOptions& options, std::ostream& err);

} // namespace ge::platform::sdl
