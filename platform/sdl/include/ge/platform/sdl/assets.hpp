#pragma once

// Reading the files of a game the same way wherever they are: in a folder next to the program
// on a computer, in the `assets/` of the APK on Android (where std::ifstream cannot see them,
// and a folder cannot be walked). SDL reads both.

#include <optional>
#include <string>
#include <vector>

namespace ge::platform::sdl {

/// The whole file at `path` (UTF-8), from the disk or from the APK; nothing when it cannot be read.
[[nodiscard]] std::optional<std::string> read_file(const std::string& path);

/// The files of a game, relative to its folder, in order. An exported game lists them in
/// `files.txt` (one path a line) because an APK has no folders to walk; a game in the
/// repository has none, so its folder is walked.
[[nodiscard]] std::vector<std::string> list_game_files(const std::string& folder);

/// The name of the file the export writes: every file of the package's `data/`, one path a line.
inline constexpr const char* file_index_name = "files.txt";

} // namespace ge::platform::sdl
