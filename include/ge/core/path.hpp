#pragma once

// Paths as the engine writes them are UTF-8 text. On Windows, `std::filesystem::path` built from a
// `std::string` reads it in the system's ANSI code page instead, so a folder with an accent
// ("C:\Users\Léa\Games") cannot be opened. These two functions say it is UTF-8, on every system.

#include <filesystem>
#include <string>

namespace ge::core {

/// The path written in `text`, which is UTF-8.
[[nodiscard]] inline std::filesystem::path path_from_utf8(const std::string& text) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): char and char8_t share a layout
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

/// The path as UTF-8 text with `/` separators.
[[nodiscard]] inline std::string path_to_utf8(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): char8_t and char share a layout
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

} // namespace ge::core
