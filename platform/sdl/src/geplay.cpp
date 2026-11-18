#include "ge/platform/sdl/geplay.hpp"

#include "ge/app/save.hpp"
#include "ge/app/viewport.hpp"
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
    "                        [--save FILE]\n"
    "\n"
    "Keys and buttons act on the game through the game's input file. The\n"
    "game's sounds are the .wav files of its sounds/ directory. The window\n"
    "closes when you close it, or after N frames. --headless opens no window\n"
    "and plays no sound; --screenshot saves the last frame as a .bmp file.\n"
    "What the game keeps between two runs (high scores) is read from and written\n"
    "to FILE; by default a file in your application-data folder, except with\n"
    "--headless, which keeps nothing unless --save is given.\n"
    "Exit codes: 0 success, 1 invalid game or cannot start, 2 usage.\n";

struct Arguments {
    std::string game_path;
    std::uint64_t frames = 0; ///< 0: until the window closes.
    int width = 0;            ///< 0: the size the game asks for (`window` in game.json).
    int height = 0;
    bool headless = false;
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

std::string parse_size(const std::string& text, Arguments& out) {
    const std::size_t x = text.find('x');
    if (x == std::string::npos || !parse_number(text.substr(0, x), out.width) ||
        !parse_number(text.substr(x + 1), out.height)) {
        return "--size needs WIDTHxHEIGHT, for example 800x600, not \"" + text + "\"";
    }
    return {};
}

/// Returns an error message, or empty.
std::string parse(const std::vector<std::string>& args, Arguments& out) {
    std::string error;
    for (std::size_t i = 0; i < args.size() && error.empty(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--frames") {
            const std::string* text = value_of(args, i, "a number", error);
            error = text != nullptr ? parse_frames(*text, out.frames) : error;
        } else if (arg == "--size") {
            const std::string* text = value_of(args, i, "WIDTHxHEIGHT, for example 800x600", error);
            error = text != nullptr ? parse_size(*text, out) : error;
        } else if (arg == "--screenshot") {
            const std::string* text = value_of(args, i, "a file name", error);
            out.screenshot = text != nullptr ? *text : out.screenshot;
        } else if (arg == "--save") {
            const std::string* text = value_of(args, i, "a file name", error);
            out.save = text != nullptr ? *text : out.save;
        } else if (arg == "--headless") {
            out.headless = true;
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

/// Where the save data lives: the file asked for, else the user's application-data folder
/// (not for a headless run, which must leave nothing behind), else nowhere.
std::string save_path_of(const Arguments& arguments, const std::string& game_name) {
    if (!arguments.save.empty() || arguments.headless) {
        return arguments.save;
    }
    const std::string directory = user_data_directory(game_name);
    return directory.empty() ? std::string() : directory + "save.txt";
}

void write_text(const std::string& path, const std::string& text, std::ostream& err) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
    if (!stream) {
        err << "geplay: cannot write the save file " << path << '\n';
    }
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
    window.width =
        arguments.width > 0 ? arguments.width : static_cast<int>(loaded.game.manifest.window_width);
    window.height = arguments.height > 0 ? arguments.height
                                         : static_cast<int>(loaded.game.manifest.window_height);
    // The interface anchors itself to the real size of the window.
    app.world().insert_resource(app::Viewport{static_cast<std::uint32_t>(window.width),
                                              static_cast<std::uint32_t>(window.height)});
    window.headless = arguments.headless;
    window.capture = !arguments.screenshot.empty();
    std::string error;
    const std::unique_ptr<SdlBackend> backend = SdlBackend::create(window, error);
    if (backend == nullptr) {
        err << "geplay: " << error << '\n';
        return exit_invalid;
    }
    load_sounds(*backend, arguments.game_path, err);
    // A window that stays black is baffling, so say why when nothing can draw.
    const auto& used_modules = loaded.game.manifest.modules;
    const bool can_draw = std::ranges::any_of(used_modules, [](const std::string& name) {
        return name == "debug_draw" || name == "sprite2d";
    });
    if (!can_draw) {
        err << arguments.game_path << ": note: the game uses no drawing module (debug_draw or "
            << "sprite2d), so the window will stay empty\n";
    }
    const std::vector<std::string> atlas_problems =
        game::load_atlases(loaded.game, *backend->renderer());
    for (const std::string& problem : atlas_problems) {
        err << arguments.game_path << ": " << problem << '\n';
    }
    if (!atlas_problems.empty()) {
        return exit_invalid;
    }

    LoopOptions options;
    options.max_frames = arguments.frames;
    options.back_sources = {"key:escape"}; // Escape leaves, as in the launcher
    const std::string save_path = save_path_of(arguments, loaded.game.manifest.name);
    if (!save_path.empty()) {
        if (const std::optional<std::string> text = read_file(save_path); text) {
            app.world().resource<app::SaveData>()->from_text(*text);
        }
        options.after_frame = [&save_path, &err](app::App& running) {
            auto* save = running.world().resource<app::SaveData>();
            if (save != nullptr && save->dirty()) {
                write_text(save_path, save->to_text(), err);
                save->mark_clean();
            }
        };
    }
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
