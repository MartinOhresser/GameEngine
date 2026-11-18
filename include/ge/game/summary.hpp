#pragma once

#include "ge/game/game.hpp"
#include "ge/json/json.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ge::game {

/// What a launcher or a packager needs to know about a game without running
/// it: how it presents itself, how big its window is, what it is made of, and
/// whether it is valid.
struct GameSummary {
    /// The directory of the game, as given (for example "games/doodle_jump").
    std::string directory;
    std::string name;
    /// The manifest's `title`, or the name made readable ("doodle_jump" gives "Doodle jump").
    std::string title;
    std::string description;
    std::string version;
    /// The manifest's `id`, or "ge.games." followed by the name.
    std::string id;
    /// The icon's path relative to the directory, empty when the game has none.
    std::string icon;
    std::uint32_t window_width = 0;
    std::uint32_t window_height = 0;
    std::vector<std::string> modules;
    std::size_t prefabs = 0;
    std::size_t scenes = 0;
    std::size_t scenarios = 0;
    bool valid = false;
    /// Every problem found while loading, as "file: path: message" (empty when valid).
    std::vector<std::string> problems;
};

/// "doodle_jump" gives "Doodle jump".
[[nodiscard]] std::string default_title(std::string_view name);

/// "doodle_jump" gives "ge.games.doodle_jump".
[[nodiscard]] std::string default_id(std::string_view name);

/// Describes a loaded game. An invalid game is still described as far as its
/// manifest could be read; `fallback_name` stands for a name that could not be.
[[nodiscard]] GameSummary summarize(const LoadedGame& loaded, std::string_view directory,
                                    std::string_view fallback_name);

/// The summary as JSON, in the shape `gectl list --json` prints.
[[nodiscard]] json::Value to_json(const GameSummary& summary);

} // namespace ge::game
