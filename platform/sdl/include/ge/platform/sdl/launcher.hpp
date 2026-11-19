#pragma once

// The launcher's host: the part of the launcher that a game cannot do. The launcher
// itself is a game made of JSON files (`tools/launcher/`, the same `ui` and `text2d`
// as any game); this host reads the disk for it (the games), starts the game that was
// chosen on the same window, and brings the launcher back when that game is left.

#include "ge/game/runtime.hpp"
#include "ge/game/summary.hpp"
#include "ge/platform/backend.hpp"

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace ge::platform::sdl {

/// What an export has said since it was last asked.
struct ExportProgress {
    /// New lines of output, in order.
    std::vector<std::string> lines;
    /// An export is going on.
    bool running = false;
    /// An export ended since the last time; `succeeded` says how.
    bool finished = false;
    bool succeeded = false;
};

/// Makes the zip of a game for a platform, in the background. The launcher only asks and
/// shows what comes back; what an export is made of (a compiler, CMake) belongs to the program
/// that creates the launcher, so the launcher itself needs none of it.
class ExportService {
public:
    ExportService() = default;
    ExportService(const ExportService&) = delete;
    ExportService& operator=(const ExportService&) = delete;
    ExportService(ExportService&&) = delete;
    ExportService& operator=(ExportService&&) = delete;
    virtual ~ExportService() = default;

    /// The platforms offered, as a player reads them ("Windows (64-bit)"), in the order that
    /// `start` numbers them.
    [[nodiscard]] virtual std::vector<std::string> targets() const = 0;
    /// Starts the export of the game whose manifest is `manifest` for platform number
    /// `target`. False, with `error` filled, when it cannot start (one export at a time).
    [[nodiscard]] virtual bool start(const std::string& manifest, std::size_t target,
                                     std::string& error) = 0;
    /// The output since the last call, and whether an export is going on or just ended.
    [[nodiscard]] virtual ExportProgress poll() = 0;
};

struct LauncherOptions {
    /// The folder whose subfolders are the games.
    std::string games_dir = "games";
    /// Stop the launcher after this many frames of its own; 0 means until the player quits.
    std::uint64_t max_frames = 0;
    /// Where a game keeps its save data, from the game's name; no saving when empty.
    std::function<std::string(const std::string&)> save_path_of;
    /// Makes exports; none means the launcher offers no platform to export for.
    ExportService* exporter = nullptr;
};

struct LauncherResult {
    bool started = false;
    /// The names of the games that were played, in order.
    std::vector<std::string> played;
    /// The lines of the export screen as they were when the launcher ended.
    std::vector<std::string> export_lines;
};

/// The lines of the export screen (the run-time list "export"): the heading, the state, then
/// the last lines of output.
inline constexpr std::size_t export_line_count = 10;
inline constexpr std::size_t export_log_lines = export_line_count - 2;

/// The lines the launcher shows about a game (the run-time list "details"): title, version,
/// the description cut to the width of the panel, the window, the modules. Always the same
/// number of lines, so that the launcher's texts can be placed once.
inline constexpr std::size_t detail_line_count = 10;
[[nodiscard]] std::vector<std::string> detail_lines(const game::GameSummary& summary);

/// Runs the launcher on `backend` until the player quits it (its Quit button, Escape or
/// the window). The games are listed in the run-time list "games" and the selected one's
/// lines in "details"; a game's "play" command starts that game, whatever the launcher
/// was doing is paused, and the launcher comes back, with its selection, when the game
/// ends (its own Quit button, Escape) or until the window is closed.
LauncherResult run_launcher(Backend& backend, const game::LoadedGame& launcher,
                            const std::string& launcher_manifest,
                            std::span<const game::ModuleEntry> modules,
                            const LauncherOptions& options, std::ostream& err);

/// gelauncher: the launcher in a window.
///
///     gelauncher [--games DIR] [--launcher GAME.json] [--source DIR] [--dist DIR]
///                [--headless] [--frames N] [--screenshot FILE.bmp]
///
/// `make_exporter` builds the export service from the engine's folder (`--source`) and the
/// folder of the zips (`--dist`); without it the launcher cannot export.
/// Exit codes: 0 success, 1 invalid launcher or a failure to start, 2 usage.
using ExporterFactory = std::function<std::unique_ptr<ExportService>(const std::string& source_dir,
                                                                     const std::string& dist_dir)>;
int run_gelauncher(const std::vector<std::string>& args, std::span<const game::ModuleEntry> modules,
                   std::ostream& out, std::ostream& err, const ExporterFactory& make_exporter = {});

} // namespace ge::platform::sdl
