#pragma once

// `gectl export`: a game as a zip that anyone can run (see ADR 0011). The zip holds one
// program, the game's files in `data/`, a README and the licenses; nothing to install.

#include "ge/game/summary.hpp"

#include <filesystem>
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

namespace ge::tools {

struct ExportJob;

/// A platform a game can be exported for. A new platform is a new target; nothing else
/// changes (Android, iOS, macOS and consoles will be some).
///
/// An export has the same steps everywhere: check the game, replay its tests, then the two that
/// belong to the platform: `build` makes the program (a compiler and CMake on a desktop, with
/// Gradle on top of them for Android) and `assemble` makes what is sent from it and from the
/// game's files (a zip, an APK).
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
    /// ".exe" for Windows, nothing elsewhere. Used by the desktop steps; other platforms keep
    /// the default.
    [[nodiscard]] virtual std::string executable_suffix() const { return {}; }
    /// Extra `-D` arguments for CMake: static runtime, for instance. Desktop only.
    [[nodiscard]] virtual std::vector<std::string> configure_arguments() const { return {}; }
    /// The part of the README that depends on the platform: how to start the game, where it
    /// keeps its progress, what the system may say about an unsigned program.
    [[nodiscard]] virtual std::string readme_platform_notes(const std::string& executable,
                                                            const std::string& game_name) const = 0;

    /// Step 1: makes the program of the game in `job.build_dir`. `program` is where it is
    /// afterwards. False, with the reason written to `err`, when it could not.
    [[nodiscard]] virtual bool build(const ExportJob& job, std::string& program, std::ostream& out,
                                     std::ostream& err) const = 0;
    /// Step 2: makes what is sent in `job.out_dir` from the program and the game's files
    /// (`stage_game_files`). `artifact` is the file that was made. False, with the reason
    /// written to `err`, when it could not.
    [[nodiscard]] virtual bool assemble(const ExportJob& job, const std::string& program,
                                        std::string& artifact, std::ostream& out,
                                        std::ostream& err) const = 0;
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
    /// The processors an Android package is made for ("arm64-v8a", "x86_64" for an emulator); empty
    /// means the target's default (arm64-v8a, every recent phone).
    std::vector<std::string> abis;
};

/// Runs a program with its arguments and returns its exit code. The default runs it for real
/// (CMake builds the player and makes the zip); tests give one that pretends.
using CommandRunner = std::function<int(const std::vector<std::string>& command)>;

/// What the steps of a target share: the game, checked, and where things go. Made by
/// `export_game`.
struct ExportJob {
    const ExportRequest* request = nullptr;
    game::LoadedGame loaded;
    game::GameSummary summary;
    /// The game's folder, with its trailing separator ("games/doodle_jump/").
    std::string folder;
    /// The modules the program contains: those of the game and what they depend on.
    std::vector<std::string> modules;
    /// Where the program is made.
    std::filesystem::path build_dir;
    /// Where what is sent goes.
    std::filesystem::path out_dir;
    /// The name of the package, without extension: "Doodle jump 1.0.0 windows".
    std::string package;
    /// Runs the programs the steps need (CMake, Gradle).
    CommandRunner run;
};

/// Copies the files of the game that the package needs (those it uses, not the scenarios or the
/// sources of the art) to `data_dir`, with its manifest reduced to what a player needs.
[[nodiscard]] bool stage_game_files(const ExportJob& job, const std::filesystem::path& data_dir,
                                    std::ostream& err);

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

/// Reads and checks the game of `request` and fills `job` for `target`: what `export_game` does
/// before the steps of the target. False, with the problems written to `err`, when the game is
/// not valid.
[[nodiscard]] bool prepare_export_job(const ExportRequest& request, const ExportTarget& target,
                                      const CommandRunner& run, ExportJob& job, std::ostream& err);

/// Makes the package of a game: validates it, replays its scenarios, builds its player,
/// gathers the files and zips them. Returns 0 on success, 1 when something went wrong (a
/// message is written to `err`). `run` defaults to `run_process`.
int export_game(const ExportRequest& request, std::ostream& out, std::ostream& err,
                const CommandRunner& run = run_process);

/// The targets, one per line: name, label and whether this machine can make it.
void print_export_targets(std::ostream& out);

} // namespace ge::tools
