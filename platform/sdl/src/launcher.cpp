#include "ge/platform/sdl/launcher.hpp"

#include "ge/app/host.hpp"
#include "ge/app/lists.hpp"
#include "ge/app/runtime_lists.hpp"
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
        const std::string joined = line.empty() ? word : line + " " + word;
        if (length_of(joined) <= panel_width || line.empty()) {
            line = joined;
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
            std::vector<game::GameSummary> games, std::ostream& out, std::ostream& err)
        : backend_(backend), launcher_(launcher), manifest_(manifest), modules_(modules),
          options_(options), games_(std::move(games)), out_(out), err_(err) {}

    /// Sets the launcher up (false when it cannot start), then runs it.
    LauncherResult run();

private:
    bool start();
    void show_launcher();
    void update_details();
    void on_frame(app::App& running);
    /// Plays game number `index`; false when the window was closed meanwhile.
    bool play(std::uint32_t index);

    Backend& backend_;
    const game::LoadedGame& launcher_;
    const std::string& manifest_;
    std::span<const game::ModuleEntry> modules_;
    const LauncherOptions& options_;
    std::vector<game::GameSummary> games_;
    std::ostream& out_;
    std::ostream& err_;
    app::App app_;
    std::uint32_t shown_ = none; ///< The game whose lines are in "details".
    std::uint32_t chosen_ = none;
    LauncherResult result_;
};

bool Session::start() {
    const game::SetupResult setup = game::setup_game(app_, launcher_.game, modules_);
    for (const std::string& problem : setup.problems) {
        err_ << manifest_ << ": " << problem << '\n';
    }
    if (!setup.ok()) {
        return false;
    }
    app_.world().insert_resource(
        app::Viewport{launcher_.game.manifest.window_width, launcher_.game.manifest.window_height});
    std::vector<std::string> titles;
    titles.reserve(games_.size());
    for (const game::GameSummary& summary : games_) {
        titles.push_back(list_title(summary));
    }
    app_.world().resource<app::RuntimeLists>()->set("games", std::move(titles));
    update_details();
    return true;
}

/// The window and the assets of the launcher: at the start and each time it comes back.
void Session::show_launcher() {
    backend_.reset_assets();
    backend_.resize(static_cast<int>(launcher_.game.manifest.window_width),
                    static_cast<int>(launcher_.game.manifest.window_height));
    for (const std::string& problem : game::load_atlases(launcher_.game, *backend_.renderer())) {
        err_ << manifest_ << ": " << problem << '\n';
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

void Session::on_frame(app::App& running) {
    update_details();
    for (const Command& command : take_commands(running)) {
        if (command.name != "play" || command.argument >= games_.size()) {
            continue;
        }
        if (!games_[command.argument].valid) {
            err_ << games_[command.argument].directory << ": the game is not valid, not played\n";
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
        err_ << diagnostic.to_string() << '\n';
    }
    if (!loaded.ok()) {
        return true;
    }
    PlayOptions play_options;
    if (options_.save_path_of) {
        play_options.save_path = options_.save_path_of(loaded.game.manifest.name);
    }
    result_.played.push_back(summary.name);
    const PlayResult played = play_loaded(backend_, loaded, manifest, modules_, play_options, err_);
    return !played.loop.backend_stopped;
}

LauncherResult Session::run() {
    if (!start()) {
        return result_;
    }
    result_.started = true;
    LoopOptions loop;
    loop.max_frames = options_.max_frames;
    loop.back_sources = {"key:escape"};
    loop.after_frame = [this](app::App& running) { on_frame(running); };
    while (true) {
        show_launcher();
        chosen_ = none;
        app_.world().resource<app::QuitRequest>()->requested = false;
        const LoopResult left = run_loop(app_, backend_, launcher_.game.input, loop);
        if (left.backend_stopped || chosen_ == none) {
            break; // the window was closed, or the player left the launcher
        }
        if (!play(chosen_)) {
            break;
        }
    }
    return result_;
}

} // namespace

std::vector<std::string> detail_lines(const game::GameSummary& summary) {
    std::vector<std::string> lines(detail_line_count);
    lines[0] = summary.title;
    lines[1] = "Version " + summary.version;
    const std::string description =
        summary.valid ? summary.description
                      : (summary.problems.empty() ? std::string("This game is not valid.")
                                                  : summary.problems.front());
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
                            const LauncherOptions& options, std::ostream& out, std::ostream& err) {
    std::vector<game::GameSummary> games;
    std::string error;
    if (!game::discover_games(options.games_dir, game::disk_reader(), game::catalog_of(modules),
                              games, error)) {
        err << error << '\n';
        return {};
    }
    Session session(backend, launcher, launcher_manifest, modules, options, std::move(games), out,
                    err);
    return session.run();
}

namespace {

constexpr int exit_ok = 0;
constexpr int exit_invalid = 1;
constexpr int exit_usage = 2;

constexpr const char* usage_text =
    "gelauncher - browse the games of the engine and play them, in a window\n"
    "\n"
    "Usage: gelauncher [--games DIR] [--launcher GAME.json] [--headless] [--frames N]\n"
    "                  [--screenshot FILE.bmp]\n"
    "\n"
    "--games names the folder of the games (default games), --launcher the game that\n"
    "is the launcher's own screen (default tools/launcher/game.json). Escape or the\n"
    "Quit button leaves the launcher; Escape leaves a game and comes back to it.\n"
    "--headless opens no window; --frames stops the launcher after N frames and --screenshot saves "
    "its last frame.\n"
    "Exit codes: 0 success, 1 invalid launcher or cannot start, 2 usage.\n";

struct Arguments {
    std::string games = "games";
    std::string launcher = "tools/launcher/game.json";
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
                   arg == "--screenshot") {
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
                   std::ostream& out, std::ostream& err) {
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
    options.games_dir = arguments.games;
    options.max_frames = arguments.frames;
    if (!arguments.headless) { // a headless run must leave nothing behind
        options.save_path_of = [](const std::string& game_name) {
            const std::string directory = user_data_directory(game_name);
            return directory.empty() ? std::string() : directory + "save.txt";
        };
    }
    const LauncherResult result =
        run_launcher(*backend, launcher, arguments.launcher, modules, options, out, err);
    if (result.started && !arguments.screenshot.empty() &&
        !backend->save_frame_bmp(arguments.screenshot, error)) {
        err << "gelauncher: " << error << '\n';
        return exit_invalid;
    }
    return result.started ? exit_ok : exit_invalid;
}

} // namespace ge::platform::sdl
