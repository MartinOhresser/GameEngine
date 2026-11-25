#include "ge/platform/sdl/geplay.hpp"

#include "ge/platform/sdl/play.hpp"
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
    "Usage: geplay GAME.json [--frames N] [--size WxH] [--window WxH] [--headless]\n"
    "                        [--screenshot FILE.bmp] [--save FILE] [--stats] [--no-vsync]\n"
    "\n"
    "Keys and buttons act on the game through the game's input file. The\n"
    "game's sounds are the .wav files of its sounds/ directory. The window\n"
    "closes when you close it, or after N frames. --size fixes the game's area;\n"
    "--window gives the window itself that size, so that a game that follows the\n"
    "screen (\"adapt\" in its window) takes the shape of a phone, say 360x800.\n"
    "--headless opens no window\n"
    "and plays no sound; --screenshot saves the last frame as a .bmp file.\n"
    "What the game keeps between two runs (high scores) is read from and written\n"
    "to FILE; by default a file in your application-data folder, except with\n"
    "--headless, which keeps nothing unless --save is given.\n"
    "--stats writes, every two seconds, how long the simulation and the drawing of a\n"
    "frame take; --no-vsync draws without waiting for the screen (to measure).\n"
    "Exit codes: 0 success, 1 invalid game or cannot start, 2 usage.\n";

struct Arguments {
    std::string game_path;
    std::uint64_t frames = 0; ///< 0: until the window closes.
    int width = 0;            ///< 0: the size the game asks for (`window` in game.json).
    int height = 0;
    int window_width = 0; ///< 0: the window has the size of the game's area.
    int window_height = 0;
    bool headless = false;
    bool stats = false;
    bool vsync = true;
    std::string screenshot;
    std::string save; ///< Empty: the application-data folder (a window) or nothing (headless).
};

bool parse_number(const std::string& text, int& out) {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
    return error == std::errc{} && end == text.data() + text.size() && out > 0;
}

/// The value after an option, or an error message in `error`.
const std::string* value_of(const std::vector<std::string>& args, std::size_t& i,
                            const std::string& what, std::string& error) {
    if (i + 1 >= args.size()) {
        error = args[i] + " needs " + what;
        return nullptr;
    }
    return &args[++i];
}

std::string parse_frames(const std::string& text, std::uint64_t& out) {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return "--frames needs a whole number, not \"" + text + "\"";
    }
    return {};
}

std::string parse_size(const std::string& option, const std::string& text, int& width,
                       int& height) {
    const std::size_t x = text.find('x');
    if (x == std::string::npos || !parse_number(text.substr(0, x), width) ||
        !parse_number(text.substr(x + 1), height)) {
        return option + " needs WIDTHxHEIGHT, for example 800x600, not \"" + text + "\"";
    }
    return {};
}

/// An option that takes a value: false when `arg` is not one, else the error (or empty) is in
/// `error`.
bool parse_valued(const std::vector<std::string>& args, std::size_t& i, Arguments& out,
                  std::string& error) {
    const std::string& arg = args[i];
    if (arg == "--frames") {
        const std::string* text = value_of(args, i, "a number", error);
        error = text != nullptr ? parse_frames(*text, out.frames) : error;
    } else if (arg == "--size" || arg == "--window") {
        const std::string* text = value_of(args, i, "WIDTHxHEIGHT, for example 800x600", error);
        if (text != nullptr) {
            const bool window = arg == "--window";
            error = window ? parse_size(arg, *text, out.window_width, out.window_height)
                           : parse_size(arg, *text, out.width, out.height);
        }
    } else if (arg == "--screenshot") {
        const std::string* text = value_of(args, i, "a file name", error);
        out.screenshot = text != nullptr ? *text : out.screenshot;
    } else if (arg == "--save") {
        const std::string* text = value_of(args, i, "a file name", error);
        out.save = text != nullptr ? *text : out.save;
    } else {
        return false;
    }
    return true;
}

/// Returns an error message, or empty.
std::string parse(const std::vector<std::string>& args, Arguments& out) {
    std::string error;
    for (std::size_t i = 0; i < args.size() && error.empty(); ++i) {
        const std::string& arg = args[i];
        if (parse_valued(args, i, out, error)) {
            continue;
        }
        if (arg == "--headless") {
            out.headless = true;
        } else if (arg == "--stats") {
            out.stats = true;
        } else if (arg == "--no-vsync") {
            out.vsync = false;
        } else if (arg.starts_with("--")) {
            error = "unknown option " + arg;
        } else if (out.game_path.empty()) {
            out.game_path = arg;
        } else {
            error = "only one game file can be given";
        }
    }
    if (error.empty() && out.game_path.empty()) {
        error = "no game file given";
    }
    return error;
}

/// Where the save data lives: the file asked for, else the user's application-data folder
/// (not for a headless run, which must leave nothing behind), else nowhere.
std::string save_path_of(const Arguments& arguments, const std::string& game_name) {
    if (!arguments.save.empty() || arguments.headless) {
        return arguments.save;
    }
    const std::string directory = user_data_directory(game_name);
    return directory.empty() ? std::string() : directory + "save.txt";
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
        game::load_game(arguments.game_path, game::disk_reader(), game::catalog_of(modules));
    for (const module::Diagnostic& diagnostic : loaded.diagnostics) {
        err << diagnostic.to_string() << '\n';
    }
    if (!loaded.ok()) {
        return exit_invalid;
    }

    PlayOptions play;
    play.max_frames = arguments.frames;
    play.width = arguments.width;
    play.height = arguments.height;
    play.window_width = arguments.window_width;
    play.window_height = arguments.window_height;
    play.save_path = save_path_of(arguments, loaded.game.manifest.name);
    play.stats = arguments.stats ? &out : nullptr;
    const auto [width, height] = window_size_of(loaded, play);

    WindowOptions window;
    window.title = loaded.game.manifest.name;
    window.width = width;
    window.height = height;
    window.headless = arguments.headless;
    window.vsync = arguments.vsync;
    window.capture = !arguments.screenshot.empty();
    std::string error;
    const std::unique_ptr<SdlBackend> backend = SdlBackend::create(window, error);
    if (backend == nullptr) {
        err << "geplay: " << error << '\n';
        return exit_invalid;
    }
    const PlayResult result =
        play_loaded(*backend, loaded, arguments.game_path, modules, play, err);
    if (!result.started) {
        return exit_invalid;
    }
    out << loaded.game.manifest.name << ": " << result.loop.frames << " frames, " << result.ticks
        << " ticks\n";

    if (!arguments.screenshot.empty() && !backend->save_frame_bmp(arguments.screenshot, error)) {
        err << "geplay: " << error << '\n';
        return exit_invalid;
    }
    return exit_ok;
}

} // namespace ge::platform::sdl
