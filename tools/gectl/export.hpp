#pragma once

// `gectl export`: a game as a zip that anyone can run (see ADR 0011). The zip holds one
// program, the game's files in `data/`, a README and the licenses; nothing to install.

#include "ge/game/summary.hpp"

#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

namespace ge::tools {

/// A platform a game can be exported for. A new platform is a new target; nothing else
/// changes (Android, iOS, macOS and consoles will be some).
class ExportTarget {
public:
    ExportTarget() = default;
    ExportTarget(const ExportTarget&) = delete;
    ExportTarget& operator=(const ExportTarget&) = delete;
    ExportTarget(ExportTarget&&) = delete;
    ExportTarget& operator=(ExportTarget&&) = delete;
    virtual ~ExportTarget() = default;

    /// The name given to `--target`: "windows".
    [[nodiscard]] virtual std::string name() const = 0;
    /// What a person is shown: "Windows (64-bit)".
    [[nodiscard]] virtual std::string label() const = 0;
    /// A target for tests only, not offered to players.
    [[nodiscard]] virtual bool hidden() const { return false; }
    /// Whether this machine can make the package (the compiler of that platform is local).
    [[nodiscard]] virtual bool buildable_here() const = 0;
    /// What to do when it cannot.
    [[nodiscard]] virtual std::string needs() const = 0;
    /// ".exe" for Windows, nothing elsewhere.
    [[nodiscard]] virtual std::string executable_suffix() const = 0;
    /// Extra `-D` arguments for CMake: static runtime, for instance.
    [[nodiscard]] virtual std::vector<std::string> configure_arguments() const = 0;
    /// The part of the README that depends on the platform: how to start the game, where it
    /// keeps its progress, what the system may say about an unsigned program.
    [[nodiscard]] virtual std::string readme_platform_notes(const std::string& executable,
                                                            const std::string& game_name) const = 0;
};

/// The targets that exist, `hidden` ones included only when asked.
[[nodiscard]] std::vector<const ExportTarget*> export_targets(bool include_hidden);

/// The target called `name` (hidden ones too), or null.
[[nodiscard]] const ExportTarget* find_export_target(const std::string& name);

struct ExportRequest {
    /// The game's manifest, as it was given (`games/doodle_jump/game.json`).
    std::string manifest;
    std::string target = "windows";
    /// Where the zip goes.
    std::string out_dir = "dist";
    /// The engine's sources (where its CMakeLists.txt is).
    std::string source_dir = ".";
    /// Where the player is compiled; empty means `<source>/build/export/<target>`.
    std::string build_dir;
    /// Do not replay the game's scenarios first.
    bool skip_tests = false;
    /// More `-D...` arguments for CMake (a local copy of SDL3, for instance).
    std::vector<std::string> cmake_arguments;
};

/// Runs a program with its arguments and returns its exit code. The default runs it for real
/// (CMake builds the player and makes the zip); tests give one that pretends.
using CommandRunner = std::function<int(const std::vector<std::string>& command)>;

/// Runs a command as a new process, its output shown as it comes.
[[nodiscard]] int run_process(const std::vector<std::string>& command);

/// Runs a command as a new process and hands each line it writes (output and errors) to
/// `on_line` instead of showing it. A launcher uses it to show the progress of an export.
[[nodiscard]] int run_process_lines(const std::vector<std::string>& command,
                                    const std::function<void(const std::string&)>& on_line);

/// A name that is safe as a file name on every system: letters, digits, space, `-`, `_`, `.`.
[[nodiscard]] std::string safe_file_name(const std::string& text);

/// A PNG as an .ico file with that single picture inside (the PNG is stored as it is, which
/// Windows has understood since Vista). False, with `error` filled, if it is not a PNG of at
/// most 256 by 256 pixels.
[[nodiscard]] bool png_to_ico(const std::string& png, std::string& ico, std::string& error);

/// The README of a package. `input_text` is the game's input file (empty if it has none), read
/// for the keys of the game.
[[nodiscard]] std::string make_readme(const game::GameSummary& summary,
                                      const std::string& input_text, const ExportTarget& target,
                                      const std::string& executable);

/// The name of the folder (and of the zip) of a package: "Doodle jump 1.0.0 windows".
[[nodiscard]] std::string package_name(const game::GameSummary& summary,
                                       const ExportTarget& target);

/// The text of LICENSES.txt: the engine's license and those of what the player contains.
/// `engine_license` and `sdl_license` are the texts of the files, empty when not found.
[[nodiscard]] std::string make_licenses(const std::string& engine_license,
                                        const std::string& sdl_license);

/// Makes the package of a game: validates it, replays its scenarios, builds its player,
/// gathers the files and zips them. Returns 0 on success, 1 when something went wrong (a
/// message is written to `err`). `run` defaults to `run_process`.
int export_game(const ExportRequest& request, std::ostream& out, std::ostream& err,
                const CommandRunner& run = run_process);

/// The targets, one per line: name, label and whether this machine can make it.
void print_export_targets(std::ostream& out);

} // namespace ge::tools
