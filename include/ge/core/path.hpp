#pragma once

// Paths as the engine writes them are UTF-8 text. On Windows, `std::filesystem::path` built from a
// `std::string` reads it in the system's ANSI code page instead, so a folder with an accent
// ("C:\Users\Léa\Games") cannot be opened. These two functions say it is UTF-8, on every system.

#include <filesystem>
#include <string>

namespace ge::core {

/// The path written in `text`, which is UTF-8.
[[nodiscard]] inline std::filesystem::path path_from_utf8(const std::string& text) {
    // char to char8_t keeps every byte as it is (same size, modulo 256).
    const std::u8string bytes(text.begin(), text.end());
    return {bytes};
}

/// The path as UTF-8 text with `/` separators.
[[nodiscard]] inline std::string path_to_utf8(const std::filesystem::path& path) {
    const std::u8string bytes = path.generic_u8string();
    return {bytes.begin(), bytes.end()};
}

} // namespace ge::core
