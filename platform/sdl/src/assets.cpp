#include "ge/platform/sdl/assets.hpp"

#include "ge/core/path.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <system_error>

namespace ge::platform::sdl {

std::optional<std::string> read_file(const std::string& path) {
    std::size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (data == nullptr) {
        return std::nullopt;
    }
    std::string text(static_cast<const char*>(data), size);
    SDL_free(data);
    return text;
}

namespace {

std::vector<std::string> walk(const std::string& folder) {
    namespace fs = std::filesystem;
    std::vector<std::string> found;
    std::error_code error;
    const fs::path root = core::path_from_utf8(folder.empty() ? "." : folder);
    for (fs::recursive_directory_iterator it(root, error), end; !error && it != end;
         it.increment(error)) {
        if (it->is_regular_file(error)) {
            found.push_back(core::path_to_utf8(it->path().lexically_relative(root)));
        }
    }
    return found;
}

} // namespace

std::vector<std::string> list_game_files(const std::string& folder) {
    std::vector<std::string> files;
    if (const std::optional<std::string> index = read_file(folder + file_index_name); index) {
        std::istringstream lines(*index);
        for (std::string line; std::getline(lines, line);) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (!line.empty()) {
                files.push_back(line);
            }
        }
    } else {
        files = walk(folder);
    }
    std::ranges::sort(files);
    return files;
}

} // namespace ge::platform::sdl
