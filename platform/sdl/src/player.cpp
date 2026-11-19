#include "ge/platform/sdl/player.hpp"

#include "ge/game/summary.hpp"
#include "ge/platform/sdl/play.hpp"
#include "ge/platform/sdl/sdl_backend.hpp"

#include <charconv>
#include <memory>
#include <ostream>

namespace ge::platform::sdl {

namespace {

constexpr int exit_ok = 0;
constexpr int exit_invalid = 1;
constexpr int exit_usage = 2;

struct Arguments {
    std::string data;
    bool headless = false;
    bool software = false;
    std::uint64_t frames = 0;
    std::string screenshot;
};

/// Returns an error message, or empty.
std::string parse(const std::vector<std::string>& args, Arguments& out) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--headless") {
            out.headless = true;
            continue;
        }
        if (arg == "--software") {
            out.software = true;
            continue;
        }
        if (arg != "--data" && arg != "--frames" && arg != "--screenshot") {
            return "unknown argument " + arg;
        }
        if (i + 1 >= args.size()) {
            return arg + " needs a value";
        }
        const std::string& value = args[++i];
        if (arg == "--data") {
            out.data = value;
        } else if (arg == "--screenshot") {
            out.screenshot = value;
        } else {
            const auto [end, error] =
                std::from_chars(value.data(), value.data() + value.size(), out.frames);
            if (error != std::errc{} || end != value.data() + value.size()) {
                return "--frames needs a whole number, not \"" + value + "\"";
            }
        }
    }
    return {};
}

std::string save_path_of(const Arguments& arguments, const std::string& game_name) {
    if (arguments.headless) {
        return {}; // a headless run must leave nothing behind
    }
    const std::string directory = user_data_directory(game_name);
    return directory.empty() ? std::string() : directory + "save.txt";
}

} // namespace

std::string player_title(const game::LoadedGame& loaded) {
    const game::Manifest& manifest = loaded.game.manifest;
    return manifest.title.empty() ? game::default_title(manifest.name) : manifest.title;
}

int run_player(const std::vector<std::string>& args, std::span<const game::ModuleEntry> modules,
               std::ostream& out, std::ostream& err) {
    Arguments arguments;
    if (const std::string problem = parse(args, arguments); !problem.empty()) {
        err << problem << '\n';
        return exit_usage;
    }
    std::string folder = arguments.data.empty() ? base_directory() + "data" : arguments.data;
    while (folder.size() > 1 && (folder.back() == '/' || folder.back() == '\\')) {
        folder.pop_back();
    }
    const std::string manifest = folder + "/game.json";
    const game::LoadedGame loaded =
        game::load_game(manifest, game::disk_reader(), game::catalog_of(modules));
    for (const module::Diagnostic& diagnostic : loaded.diagnostics) {
        err << diagnostic.to_string() << '\n';
    }
    if (!loaded.ok()) {
        err << "The game's files could not be read from " << folder << '\n';
        return exit_invalid;
    }

    PlayOptions play;
    play.max_frames = arguments.frames;
    play.save_path = save_path_of(arguments, loaded.game.manifest.name);
    const auto [width, height] = window_size_of(loaded, play);
    WindowOptions window;
    window.title = player_title(loaded);
    window.width = width;
    window.height = height;
    window.headless = arguments.headless;
    window.software = arguments.software;
    window.capture = !arguments.screenshot.empty();
    std::string error;
    const std::unique_ptr<SdlBackend> backend = SdlBackend::create(window, error);
    if (backend == nullptr) {
        err << error << '\n';
        return exit_invalid;
    }
    const PlayResult result = play_loaded(*backend, loaded, manifest, modules, play, err);
    if (!result.started) {
        return exit_invalid;
    }
    out << loaded.game.manifest.name << ": " << result.loop.frames << " frames\n";
    if (!arguments.screenshot.empty() && !backend->save_frame_bmp(arguments.screenshot, error)) {
        err << error << '\n';
        return exit_invalid;
    }
    return exit_ok;
}

} // namespace ge::platform::sdl
