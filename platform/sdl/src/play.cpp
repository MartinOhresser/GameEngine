#include "ge/platform/sdl/play.hpp"

#include "ge/app/host.hpp"
#include "ge/app/save.hpp"
#include "ge/app/viewport.hpp"
#include "ge/core/path.hpp"
#include "ge/platform/sdl/assets.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <ostream>
#include <string_view>

namespace ge::platform::sdl {

namespace {

std::optional<std::string> read_text(const std::string& path) {
    std::ifstream stream(core::path_from_utf8(path), std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

void write_text(const std::string& path, const std::string& text, std::ostream& err) {
    std::ofstream stream(core::path_from_utf8(path), std::ios::binary | std::ios::trunc);
    stream << text;
    if (!stream) {
        err << "cannot write the save file " << path << '\n';
    }
}

/// The .wav files of `<game directory>/sounds`, each named by its file name without extension.
void load_sounds(Backend& backend, const std::string& manifest_path, std::ostream& err) {
    const std::size_t slash = manifest_path.find_last_of("/\\");
    const std::string folder =
        slash == std::string::npos ? std::string() : manifest_path.substr(0, slash + 1);
    for (const std::string& file : list_game_files(folder)) {
        constexpr std::string_view directory = "sounds/";
        constexpr std::string_view extension = ".wav";
        const bool is_sound = file.starts_with(directory) && file.ends_with(extension) &&
                              file.find('/', directory.size()) == std::string::npos;
        if (!is_sound) {
            continue;
        }
        const std::string name =
            file.substr(directory.size(), file.size() - directory.size() - extension.size());
        std::string error;
        if (!backend.load_sound(name, folder + file, error)) {
            err << manifest_path << ": " << error << '\n';
        }
    }
}

/// A window that stays black is baffling, so say why when nothing can draw.
void warn_if_nothing_draws(const game::LoadedGame& loaded, const std::string& manifest_path,
                           std::ostream& err) {
    const auto& used = loaded.game.manifest.modules;
    const bool can_draw = std::ranges::any_of(
        used, [](const std::string& name) { return name == "debug_draw" || name == "sprite2d"; });
    if (!can_draw) {
        err << manifest_path << ": note: the game uses no drawing module (debug_draw or "
            << "sprite2d), so the window will stay empty\n";
    }
}

/// Reads the save file into the game, and arranges for it to be written when it changes.
void keep_save_data(app::App& app, const std::string& path, LoopOptions& options,
                    std::ostream& err) {
    if (path.empty()) {
        return;
    }
    if (const std::optional<std::string> text = read_text(path); text) {
        app.world().resource<app::SaveData>()->from_text(*text);
    }
    options.after_frame = [path, &err](app::App& running) {
        auto* save = running.world().resource<app::SaveData>();
        if (save != nullptr && save->dirty()) {
            write_text(path, save->to_text(), err);
            save->mark_clean();
        }
    };
}

} // namespace

std::pair<int, int> window_size_of(const game::LoadedGame& loaded, const PlayOptions& options) {
    const int width =
        options.width > 0 ? options.width : static_cast<int>(loaded.game.manifest.window_width);
    const int height =
        options.height > 0 ? options.height : static_cast<int>(loaded.game.manifest.window_height);
    return {width, height};
}

PlayResult play_loaded(Backend& backend, const game::LoadedGame& loaded,
                       const std::string& manifest_path, std::span<const game::ModuleEntry> modules,
                       const PlayOptions& options, std::ostream& err) {
    PlayResult result;
    app::App app;
    const game::SetupResult setup = game::setup_game(app, loaded.game, modules);
    for (const std::string& problem : setup.problems) {
        err << manifest_path << ": " << problem << '\n';
    }
    if (!setup.ok()) {
        return result;
    }
    if (auto* host = app.world().resource<app::HostInfo>(); host != nullptr) {
        host->mobile = options.mobile;
    }
    const auto [width, height] = window_size_of(loaded, options);
    // The interface anchors itself to the real size of the window.
    app.world().insert_resource(
        app::Viewport{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)});

    backend.reset_assets();
    backend.resize(width, height);
    load_sounds(backend, manifest_path, err);
    warn_if_nothing_draws(loaded, manifest_path, err);
    const std::vector<std::string> atlas_problems =
        game::load_atlases(loaded.game, *backend.renderer());
    for (const std::string& problem : atlas_problems) {
        err << manifest_path << ": " << problem << '\n';
    }
    if (!atlas_problems.empty()) {
        return result;
    }

    LoopOptions loop;
    loop.max_frames = options.max_frames;
    loop.back_sources = options.back_sources;
    keep_save_data(app, options.save_path, loop, err);
    result.started = true;
    result.loop = run_loop(app, backend, loaded.game.input, loop);
    result.ticks = app.tick_count();
    return result;
}

} // namespace ge::platform::sdl
