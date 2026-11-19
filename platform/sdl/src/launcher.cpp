#include "ge/platform/sdl/launcher.hpp"

#include "ge/app/host.hpp"
#include "ge/app/lists.hpp"
#include "ge/app/runtime_lists.hpp"
#include "ge/app/scene.hpp"
#include "ge/app/viewport.hpp"
#include "ge/platform/sdl/play.hpp"
#include "ge/platform/sdl/sdl_backend.hpp"

#include <algorithm>
#include <charconv>
#include <memory>
#include <ostream>
#include <sstream>

namespace ge::platform::sdl {

namespace {

constexpr std::size_t panel_width = 34; ///< Characters per line of the description.
constexpr std::size_t description_lines = 6;
constexpr std::uint32_t none = app::RuntimeLists::none;

/// How many characters a UTF-8 text has.
std::size_t length_of(const std::string& text) {
    return static_cast<std::size_t>(std::ranges::count_if(
        text, [](char c) { return (static_cast<unsigned char>(c) & 0xC0U) != 0x80U; }));
}

/// The text cut into lines of at most `panel_width` characters, at spaces; the last allowed
/// line ends with an ellipsis when there is more.
std::vector<std::string> wrap(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream words(text);
    std::string word;
    std::string line;
    bool truncated = false;
    while (words >> word) {
        std::string joined = line;
        if (!joined.empty()) {
            joined += ' ';
        }
        joined += word;
        if (length_of(joined) <= panel_width || line.empty()) {
            line = std::move(joined);
            continue;
        }
        lines.push_back(line);
        line = word;
        if (lines.size() == description_lines) {
            truncated = true;
            break;
        }
    }
    if (!truncated && !line.empty()) {
        lines.push_back(line);
    }
    if (truncated) {
        lines.back() += "\xE2\x80\xA6"; // the ellipsis
    }
    return lines;
}

std::string list_title(const game::GameSummary& summary) {
    return summary.valid ? summary.title : summary.title + " (invalid)";
}

/// What the host does with the commands a game left, as index of the `host_command` list.
struct Command {
    std::string name;
    std::uint32_t argument = 0;
};

std::vector<Command> take_commands(app::App& app) {
    std::vector<Command> commands;
    auto* requests = app.world().resource<app::HostRequests>();
    const auto* names = app.world().resource<app::ListNames>();
    if (requests == nullptr || names == nullptr) {
        return commands;
    }
    for (const app::HostRequest& request : requests->pending) {
        commands.push_back(
            Command{std::string(names->name("host_command", request.command)), request.argument});
    }
    requests->pending.clear();
    return commands;
}

/// Everything the launcher keeps between two games.
class Session {
public:
    Session(Backend& backend, const game::LoadedGame& launcher, const std::string& manifest,
            std::span<const game::ModuleEntry> modules, const LauncherOptions& options,
            std::vector<game::GameSummary> games, std::ostream& err)
        : backend_(&backend), launcher_(&launcher), manifest_(&manifest), modules_(modules),
          options_(&options), games_(std::move(games)), err_(&err) {}

    /// Sets the launcher up (false when it cannot start), then runs it.
    LauncherResult run();

private:
    bool start();
    void show_launcher();
    void update_details();
    void update_export_screen();
    void start_export(std::uint32_t target);
    void say(std::string state);
    /// The scene the launcher is in has this name.
    bool in_scene(const std::string& name);
    /// Asks for the scene with this name; between two ticks the launcher changes to it.
    void go_to_scene(const std::string& name);
    void on_frame(app::App& running);
    /// Plays game number `index`; false when the window was closed meanwhile.
    bool play(std::uint32_t index);

    Backend* backend_;
    const game::LoadedGame* launcher_;
    const std::string* manifest_;
    std::span<const game::ModuleEntry> modules_;
    const LauncherOptions* options_;
    std::vector<game::GameSummary> games_;
    std::ostream* err_;
    app::App app_;
    std::uint32_t shown_ = none; ///< The game whose lines are in "details".
    std::uint32_t chosen_ = none;
    LauncherResult result_;
    // The export screen: what it shows, and whether an export is going on.
    bool exporting_ = false;
    std::string export_state_;
    std::vector<std::string> export_log_;
    std::vector<std::string> export_shown_;
};

bool Session::start() {
    const game::SetupResult setup = game::setup_game(app_, launcher_->game, modules_);
    for (const std::string& problem : setup.problems) {
        *err_ << *manifest_ << ": " << problem << '\n';
    }
    if (!setup.ok()) {
        return false;
    }
    app_.world().insert_resource(app::Viewport{launcher_->game.manifest.window_width,
                                               launcher_->game.manifest.window_height});
    std::vector<std::string> titles;
    titles.reserve(games_.size());
    for (const game::GameSummary& summary : games_) {
        titles.push_back(list_title(summary));
    }
    app_.world().resource<app::RuntimeLists>()->set("games", std::move(titles));
    app_.world().resource<app::RuntimeLists>()->set("targets", options_->exporter != nullptr
                                                                   ? options_->exporter->targets()
                                                                   : std::vector<std::string>());
    export_state_ =
        options_->exporter != nullptr ? "Choose a platform." : "No platform to export for here.";
    update_details();
    update_export_screen();
    return true;
}

/// The window and the assets of the launcher: at the start and each time it comes back.
void Session::show_launcher() {
    backend_->reset_assets();
    backend_->resize(static_cast<int>(launcher_->game.manifest.window_width),
                     static_cast<int>(launcher_->game.manifest.window_height));
    for (const std::string& problem : game::load_atlases(launcher_->game, *backend_->renderer())) {
        *err_ << *manifest_ << ": " << problem << '\n';
    }
}

void Session::update_details() {
    auto* lists = app_.world().resource<app::RuntimeLists>();
    const std::uint32_t selected = lists->selected("games");
    if (selected == shown_) {
        return;
    }
    shown_ = selected;
    lists->set("details", selected < games_.size() ? detail_lines(games_[selected])
                                                   : std::vector<std::string>());
}

/// A line of output as the export screen shows it: cut to the width of the window.
std::string fit_line(const std::string& line) {
    constexpr std::size_t width = 84;
    std::string text;
    std::size_t count = 0;
    for (const char c : line) {
        if (c == '\r' || c == '\t') {
            continue;
        }
        if ((static_cast<unsigned char>(c) & 0xC0U) != 0x80U && ++count > width) {
            return text + "\xE2\x80\xA6";
        }
        text += c;
    }
    return text;
}

void Session::say(std::string state) {
    export_state_ = std::move(state);
}

void Session::start_export(std::uint32_t target) {
    if (options_->exporter == nullptr) {
        return;
    }
    const std::uint32_t selected = app_.world().resource<app::RuntimeLists>()->selected("games");
    if (selected >= games_.size()) {
        say("Choose a game first.");
        return;
    }
    if (!games_[selected].valid) {
        say("This game is not valid, so it cannot be exported.");
        return;
    }
    std::string error;
    export_log_.clear();
    if (!options_->exporter->start(games_[selected].directory + "/game.json", target, error)) {
        say(error.empty() ? "The export could not start." : error);
        return;
    }
    exporting_ = true;
    say("Working... this can take a few minutes the first time.");
}

/// Brings what the export says into the lists of the export screen.
void Session::update_export_screen() {
    if (options_->exporter != nullptr) {
        const ExportProgress progress = options_->exporter->poll();
        for (const std::string& line : progress.lines) {
            export_log_.push_back(fit_line(line));
        }
        if (export_log_.size() > export_log_lines) {
            export_log_.erase(export_log_.begin(),
                              export_log_.end() - static_cast<std::ptrdiff_t>(export_log_lines));
        }
        exporting_ = progress.running;
        if (progress.finished) {
            say(progress.succeeded ? "Done. The zip is in the dist folder."
                                   : "The export failed. See the lines below.");
        }
    }
    const std::uint32_t selected = app_.world().resource<app::RuntimeLists>()->selected("games");
    std::vector<std::string> lines(export_line_count);
    lines[0] = selected < games_.size() ? games_[selected].title : std::string("No game chosen");
    lines[1] = export_state_;
    for (std::size_t i = 0; i < export_log_.size(); ++i) {
        lines[2 + i] = export_log_[i];
    }
    if (lines != export_shown_) {
        export_shown_ = lines;
        app_.world().resource<app::RuntimeLists>()->set("export", std::move(lines));
    }
}

bool Session::in_scene(const std::string& name) {
    const auto* names = app_.world().resource<app::ListNames>();
    const auto* current = app_.world().resource<app::CurrentScene>();
    return names != nullptr && current != nullptr && names->name("scene", current->index) == name;
}

void Session::go_to_scene(const std::string& name) {
    const auto* names = app_.world().resource<app::ListNames>();
    if (names == nullptr) {
        return;
    }
    for (const auto& list : names->lists) {
        if (list.first != "scene") {
            continue;
        }
        const auto found = std::ranges::find(list.second, name);
        if (found != list.second.end()) {
            app_.world().resource<app::SceneRequest>()->scene =
                static_cast<std::uint32_t>(found - list.second.begin());
        }
    }
}

void Session::on_frame(app::App& running) {
    update_details();
    update_export_screen();
    for (const Command& command : take_commands(running)) {
        if (command.name == "export") {
            start_export(command.argument);
            continue;
        }
        if (command.name != "play" || command.argument >= games_.size()) {
            continue;
        }
        if (!games_[command.argument].valid) {
            *err_ << games_[command.argument].directory << ": the game is not valid, not played\n";
            continue;
        }
        chosen_ = command.argument;
        running.world().resource<app::QuitRequest>()->requested = true; // leaves the loop
    }
}

bool Session::play(std::uint32_t index) {
    const game::GameSummary& summary = games_[index];
    const std::string manifest = summary.directory + "/game.json";
    const game::LoadedGame loaded =
        game::load_game(manifest, game::disk_reader(), game::catalog_of(modules_));
    for (const module::Diagnostic& diagnostic : loaded.diagnostics) {
        *err_ << diagnostic.to_string() << '\n';
    }
    if (!loaded.ok()) {
        return true;
    }
    PlayOptions play_options;
    if (options_->save_path_of) {
        play_options.save_path = options_->save_path_of(loaded.game.manifest.name);
    }
    result_.played.push_back(summary.name);
    const PlayResult played =
        play_loaded(*backend_, loaded, manifest, modules_, play_options, *err_);
    return !played.loop.backend_stopped;
}

LauncherResult Session::run() {
    if (!start()) {
        return result_;
    }
    result_.started = true;
    LoopOptions loop;
    loop.max_frames = options_->max_frames;
    loop.back_sources = {"key:escape"};
    loop.after_frame = [this](app::App& running) { on_frame(running); };
    bool fresh = true;
    while (true) {
        if (fresh) {
            show_launcher();
        }
        fresh = true;
        chosen_ = none;
        app_.world().resource<app::QuitRequest>()->requested = false;
        const LoopResult left = run_loop(app_, *backend_, launcher_->game.input, loop);
        if (left.backend_stopped) {
            break; // the window was closed
        }
        // Escape on the export screen goes back to the games; while an export is going on it
        // does not leave the launcher (closing the window still does).
        if (left.back_requested && (in_scene("export") || exporting_)) {
            if (in_scene("export")) {
                go_to_scene("main");
            }
            fresh = false;
            continue;
        }
        if (chosen_ == none) {
            break; // the player left the launcher
        }
        if (!play(chosen_)) {
            break;
        }
    }
    result_.export_lines = export_shown_;
    return result_;
}

} // namespace

std::vector<std::string> detail_lines(const game::GameSummary& summary) {
    std::vector<std::string> lines(detail_line_count);
    lines[0] = summary.title;
    lines[1] = "Version " + summary.version;
    std::string description = summary.description;
    if (!summary.valid) {
        description =
            summary.problems.empty() ? "This game is not valid." : summary.problems.front();
    }
    std::size_t row = 2;
    for (const std::string& line : wrap(description)) {
        lines[row++] = line;
    }
    lines[8] = std::to_string(summary.window_width) + " x " +
               std::to_string(summary.window_height) + " pixels";
    lines[9] = std::to_string(summary.modules.size()) + " modules";
    return lines;
}

LauncherResult run_launcher(Backend& backend, const game::LoadedGame& launcher,
                            const std::string& launcher_manifest,
                            std::span<const game::ModuleEntry> modules,
                            const LauncherOptions& options, std::ostream& err) {
    std::vector<game::GameSummary> games;
    std::string error;
    if (!game::discover_games(options.games_dir, game::disk_reader(), game::catalog_of(modules),
                              games, error)) {
        err << error << '\n';
        return {};
    }
    Session session(backend, launcher, launcher_manifest, modules, options, std::move(games), err);
    return session.run();
}

namespace {

constexpr int exit_ok = 0;
constexpr int exit_invalid = 1;
constexpr int exit_usage = 2;

constexpr const char* usage_text =
    "gelauncher - browse the games of the engine and play them, in a window\n"
    "\n"
    "Usage: gelauncher [--games DIR] [--launcher GAME.json] [--source DIR] [--dist DIR]\n"
    "                  [--headless] [--frames N] [--screenshot FILE.bmp]\n"
    "\n"
    "--games names the folder of the games (default games), --launcher the game that\n"
    "is the launcher's own screen (default tools/launcher/game.json). --source is the\n"
    "engine's folder, where an export is built (default .), --dist where the zips go\n"
    "(default dist). Escape or the\n"
    "Quit button leaves the launcher; Escape leaves a game and comes back to it.\n"
    "--headless opens no window; --frames stops the launcher after N frames and --screenshot saves "
    "its last frame.\n"
    "Exit codes: 0 success, 1 invalid launcher or cannot start, 2 usage.\n";

struct Arguments {
    std::string games = "games";
    std::string launcher = "tools/launcher/game.json";
    std::string source = ".";
    std::string dist = "dist";
    bool headless = false;
    std::uint64_t frames = 0;
    std::string screenshot;
};

/// Returns an error message, or empty.
std::string parse(const std::vector<std::string>& args, Arguments& out) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--headless") {
            out.headless = true;
        } else if (arg == "--games" || arg == "--launcher" || arg == "--frames" ||
                   arg == "--screenshot" || arg == "--source" || arg == "--dist") {
            if (i + 1 >= args.size()) {
                return arg + " needs a value";
            }
            const std::string& value = args[++i];
            if (arg == "--games") {
                out.games = value;
            } else if (arg == "--launcher") {
                out.launcher = value;
            } else if (arg == "--screenshot") {
                out.screenshot = value;
            } else if (arg == "--source") {
                out.source = value;
            } else if (arg == "--dist") {
                out.dist = value;
            } else {
                const auto [end, error] =
                    std::from_chars(value.data(), value.data() + value.size(), out.frames);
                if (error != std::errc{} || end != value.data() + value.size()) {
                    return "--frames needs a whole number, not \"" + value + "\"";
                }
            }
        } else {
            return "unknown argument " + arg;
        }
    }
    return {};
}

} // namespace

int run_gelauncher(const std::vector<std::string>& args, std::span<const game::ModuleEntry> modules,
                   std::ostream& /*out*/, std::ostream& err, const ExporterFactory& make_exporter) {
    Arguments arguments;
    if (const std::string problem = parse(args, arguments); !problem.empty()) {
        err << problem << "\n\n" << usage_text;
        return exit_usage;
    }
    const game::LoadedGame launcher =
        game::load_game(arguments.launcher, game::disk_reader(), game::catalog_of(modules));
    for (const module::Diagnostic& diagnostic : launcher.diagnostics) {
        err << diagnostic.to_string() << '\n';
    }
    if (!launcher.ok()) {
        return exit_invalid;
    }
    WindowOptions window;
    window.title = launcher.game.manifest.name;
    window.width = static_cast<int>(launcher.game.manifest.window_width);
    window.height = static_cast<int>(launcher.game.manifest.window_height);
    window.headless = arguments.headless;
    window.capture = !arguments.screenshot.empty();
    std::string error;
    const std::unique_ptr<SdlBackend> backend = SdlBackend::create(window, error);
    if (backend == nullptr) {
        err << "gelauncher: " << error << '\n';
        return exit_invalid;
    }
    LauncherOptions options;
    std::unique_ptr<ExportService> exporter;
    if (make_exporter) {
        exporter = make_exporter(arguments.source, arguments.dist);
    }
    options.exporter = exporter.get();
    options.games_dir = arguments.games;
    options.max_frames = arguments.frames;
    if (!arguments.headless) { // a headless run must leave nothing behind
        options.save_path_of = [](const std::string& game_name) {
            const std::string directory = user_data_directory(game_name);
            return directory.empty() ? std::string() : directory + "save.txt";
        };
    }
    const LauncherResult result =
        run_launcher(*backend, launcher, arguments.launcher, modules, options, err);
    if (result.started && !arguments.screenshot.empty() &&
        !backend->save_frame_bmp(arguments.screenshot, error)) {
        err << "gelauncher: " << error << '\n';
        return exit_invalid;
    }
    return result.started ? exit_ok : exit_invalid;
}

} // namespace ge::platform::sdl
