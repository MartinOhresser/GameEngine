#include "ge/platform/sdl/geplay.hpp"

#include "ge/platform/sdl/sdl_backend.hpp"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <ostream>

namespace ge::platform::sdl {

namespace {

constexpr int exit_ok = 0;
constexpr int exit_invalid = 1;
constexpr int exit_usage = 2;

constexpr const char* usage_text =
    "geplay - play a game described by JSON files, in a window\n"
    "\n"
    "Usage: geplay GAME.json [--frames N] [--size WxH] [--headless] [--screenshot FILE.bmp]\n"
    "\n"
    "Keys and buttons act on the game through the game's input file. The\n"
    "game's sounds are the .wav files of its sounds/ directory. The window\n"
    "closes when you close it, or after N frames. --headless opens no window\n"
    "and plays no sound; --screenshot saves the last frame as a .bmp file.\n"
    "Exit codes: 0 success, 1 invalid game or cannot start, 2 usage.\n";

struct Arguments {
    std::string game_path;
    std::uint64_t frames = 0; ///< 0: until the window closes.
    int width = 800;
    int height = 600;
    bool headless = false;
    std::string screenshot;
};

bool parse_number(const std::string& text, int& out) {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
    return error == std::errc{} && end == text.data() + text.size() && out > 0;
}

/// Returns an error message, or empty.
std::string parse(const std::vector<std::string>& args, Arguments& out) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--frames") {
            if (i + 1 >= args.size()) {
                return "--frames needs a number";
            }
            const std::string& text = args[++i];
            const auto [end, error] =
                std::from_chars(text.data(), text.data() + text.size(), out.frames);
            if (error != std::errc{} || end != text.data() + text.size()) {
                return "--frames needs a whole number, not \"" + text + "\"";
            }
        } else if (arg == "--size") {
            if (i + 1 >= args.size()) {
                return "--size needs WIDTHxHEIGHT, for example 800x600";
            }
            const std::string& text = args[++i];
            const std::size_t x = text.find('x');
            if (x == std::string::npos || !parse_number(text.substr(0, x), out.width) ||
                !parse_number(text.substr(x + 1), out.height)) {
                return "--size needs WIDTHxHEIGHT, for example 800x600, not \"" + text + "\"";
            }
        } else if (arg == "--headless") {
            out.headless = true;
        } else if (arg == "--screenshot") {
            if (i + 1 >= args.size()) {
                return "--screenshot needs a file name";
            }
            out.screenshot = args[++i];
        } else if (arg.starts_with("--")) {
            return "unknown option " + arg;
        } else if (out.game_path.empty()) {
            out.game_path = arg;
        } else {
            return "only one game file can be given";
        }
    }
    return out.game_path.empty() ? "no game file given" : "";
}

std::optional<std::string> read_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

/// The .wav files of `<game directory>/sounds`, each named by its file name without extension.
std::size_t load_sounds(SdlBackend& backend, const std::string& manifest_path, std::ostream& err) {
    namespace fs = std::filesystem;
    const fs::path directory = fs::path(manifest_path).parent_path() / "sounds";
    std::error_code ignored;
    if (!fs::is_directory(directory, ignored)) {
        return 0;
    }
    std::vector<fs::path> files;
    for (const fs::directory_entry& entry : fs::directory_iterator(directory, ignored)) {
        if (entry.path().extension() == ".wav") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    std::size_t loaded = 0;
    for (const fs::path& file : files) {
        std::string error;
        if (backend.load_sound(file.stem().string(), file.string(), error)) {
            ++loaded;
        } else {
            err << manifest_path << ": " << error << '\n';
        }
    }
    return loaded;
}

} // namespace

int run_geplay(const std::vector<std::string>& args, std::span<const game::ModuleEntry> modules,
               std::ostream& out, std::ostream& err) {
    Arguments arguments;
    if (const std::string problem = parse(args, arguments); !problem.empty()) {
        err << problem << "\n\n" << usage_text;
        return exit_usage;
    }

    const game::LoadedGame loaded =
        game::load_game(arguments.game_path, read_file, game::catalog_of(modules));
    for (const module::Diagnostic& diagnostic : loaded.diagnostics) {
        err << diagnostic.to_string() << '\n';
    }
    if (!loaded.ok()) {
        return exit_invalid;
    }

    app::App app;
    const game::SetupResult setup = game::setup_game(app, loaded.game, modules);
    for (const std::string& problem : setup.problems) {
        err << arguments.game_path << ": " << problem << '\n';
    }
    if (!setup.ok()) {
        return exit_invalid;
    }

    WindowOptions window;
    window.title = loaded.game.manifest.name;
    window.width = arguments.width;
    window.height = arguments.height;
    window.headless = arguments.headless;
    window.capture = !arguments.screenshot.empty();
    std::string error;
    const std::unique_ptr<SdlBackend> backend = SdlBackend::create(window, error);
    if (backend == nullptr) {
        err << "geplay: " << error << '\n';
        return exit_invalid;
    }
    load_sounds(*backend, arguments.game_path, err);

    LoopOptions options;
    options.max_frames = arguments.frames;
    const LoopResult result = run_loop(app, *backend, loaded.game.input, options);
    out << loaded.game.manifest.name << ": " << result.frames << " frames, " << app.tick_count()
        << " ticks\n";

    if (!arguments.screenshot.empty() && !backend->save_frame_bmp(arguments.screenshot, error)) {
        err << "geplay: " << error << '\n';
        return exit_invalid;
    }
    return exit_ok;
}

} // namespace ge::platform::sdl
