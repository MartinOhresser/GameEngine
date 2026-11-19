// `gectl export`: the files of a package, its README and icon, and the sequence of commands
// that builds and zips it (a fake runner stands for CMake, so no program is compiled here).
#include "ge/game/game.hpp"
#include "ge/game/package.hpp"
#include "ge/game/summary.hpp"

#include "../support/mini_test.hpp"
#include "export.hpp"
#include "gectl.hpp"
#include "registry.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using ge::tools::ExportRequest;

namespace {

bool has(const std::string& text, const char* fragment) {
    return text.find(fragment) != std::string::npos;
}

bool contains(const std::vector<std::string>& list, const std::string& item) {
    return std::ranges::find(list, item) != list.end();
}

std::string root() {
    return GE_SOURCE_DIR;
}

ge::game::LoadedGame load_real(const std::string& name) {
    return ge::game::load_game(root() + "/games/" + name + "/game.json", ge::game::disk_reader(),
                               ge::game::catalog_of(ge::runner::linked_modules()));
}

/// A scratch folder, removed when the test ends.
class Scratch {
public:
    explicit Scratch(const std::string& name)
        : path_(fs::temp_directory_path() / ("ge_export_test_" + name)) {
        std::error_code error;
        fs::remove_all(path_, error);
        fs::create_directories(path_, error);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    Scratch(Scratch&&) = delete;
    Scratch& operator=(Scratch&&) = delete;
    ~Scratch() {
        std::error_code error;
        fs::remove_all(path_, error);
    }
    [[nodiscard]] const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

/// A copy of a game of the repository inside `scratch`, so a test may damage it.
std::string copy_game(const Scratch& scratch, const std::string& name) {
    const fs::path to = scratch.path() / "games" / name;
    std::error_code error;
    fs::create_directories(to, error);
    fs::copy(fs::path(root()) / "games" / name, to, fs::copy_options::recursive, error);
    return (to / "game.json").generic_string();
}

std::string text_of(const fs::path& path) {
    const std::ifstream stream(path, std::ios::binary);
    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
}

/// What the export asked the system to run, and the files a build would have made.
struct FakeSystem {
    std::vector<std::vector<std::string>> commands;
    int fail_at = -1; ///< The index of the command that fails, or -1.
    bool make_player = true;

    [[nodiscard]] ge::tools::CommandRunner runner() {
        return [this](const std::vector<std::string>& command) {
            const int index = static_cast<int>(commands.size());
            commands.push_back(command);
            if (index == fail_at) {
                return 1;
            }
            if (command.size() > 1 && command[1] == "--build" && make_player) {
                const fs::path player = fs::path(command[2]) / "platform" / "sdl" / "ge_player";
                std::error_code error;
                fs::create_directories(player.parent_path(), error);
                std::ofstream(player) << "program";
            }
            return 0;
        };
    }
};

ExportRequest request_in(const Scratch& scratch, const std::string& manifest) {
    ExportRequest request;
    request.manifest = manifest;
    request.target = "linux";
    request.out_dir = (scratch.path() / "dist").generic_string();
    request.source_dir = root();
    request.build_dir = (scratch.path() / "build").generic_string();
    return request;
}

std::string png_of(unsigned width, unsigned height) {
    std::string png = "\x89PNG\r\n\x1a\n";
    png += std::string("\0\0\0\x0d", 4) + "IHDR";
    for (const unsigned value : {width, height}) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            png += static_cast<char>((value >> shift) & 0xFFU);
        }
    }
    return png + "the rest of the picture";
}

} // namespace

TEST_CASE("export: file names are safe on every system") {
    CHECK(ge::tools::safe_file_name("Doodle Jump") == "Doodle Jump");
    CHECK(ge::tools::safe_file_name("a/b\\c:d*e?") == "a_b_c_d_e_");
    CHECK(ge::tools::safe_file_name("  Star Hunt. ") == "Star Hunt");
    CHECK(ge::tools::safe_file_name("...") == "game");
    CHECK(ge::tools::safe_file_name("") == "game");
}

TEST_CASE("export: a PNG becomes an icon file") {
    std::string ico;
    std::string error;
    REQUIRE(ge::tools::png_to_ico(png_of(64, 32), ico, error));
    CHECK(ico.size() == 22 + png_of(64, 32).size());
    CHECK(ico.compare(0, 6, std::string("\0\0\1\0\1\0", 6)) == 0);
    CHECK(static_cast<unsigned char>(ico[6]) == 64);
    CHECK(static_cast<unsigned char>(ico[7]) == 32);
    CHECK(static_cast<unsigned char>(ico[14]) == png_of(64, 32).size());
    CHECK(static_cast<unsigned char>(ico[18]) == 22);
    CHECK(ico.substr(22) == png_of(64, 32));

    REQUIRE(ge::tools::png_to_ico(png_of(256, 256), ico, error));
    CHECK(ico[6] == 0); // 0 stands for 256
    CHECK(ico[7] == 0);
}

TEST_CASE("export: a picture that cannot be an icon is refused") {
    std::string ico;
    std::string error;
    CHECK(!ge::tools::png_to_ico("not a picture at all, but long enough", ico, error));
    CHECK(has(error, "not a PNG"));
    CHECK(!ge::tools::png_to_ico("short", ico, error));
    CHECK(!ge::tools::png_to_ico(png_of(512, 512), ico, error));
    CHECK(has(error, "512x512"));
    CHECK(!ge::tools::png_to_ico(png_of(0, 16), ico, error));
}

TEST_CASE("export: targets") {
    CHECK(ge::tools::find_export_target("windows") != nullptr);
    CHECK(ge::tools::find_export_target("linux") != nullptr);
    CHECK(ge::tools::find_export_target("amiga") == nullptr);
    CHECK(ge::tools::export_targets(false).size() == 1);
    CHECK(ge::tools::export_targets(true).size() == 2);
    const ge::tools::ExportTarget* windows = ge::tools::find_export_target("windows");
    REQUIRE(windows != nullptr);
    CHECK(!windows->hidden());
    CHECK(windows->executable_suffix() == ".exe");
    CHECK(!windows->configure_arguments().empty());
    CHECK(!windows->needs().empty());
    CHECK(!ge::tools::find_export_target("linux")->needs().empty());
    CHECK(ge::tools::find_export_target("linux")->executable_suffix().empty());
    CHECK(ge::tools::find_export_target("linux")->configure_arguments().empty());

    std::ostringstream out;
    ge::tools::print_export_targets(out);
    CHECK(has(out.str(), "windows  Windows (64-bit)"));
    CHECK(!has(out.str(), "linux"));
}

TEST_CASE("export: the README tells how to play") {
    const auto loaded = load_real("move_sprite");
    REQUIRE(loaded.ok());
    const auto summary = ge::game::summarize(loaded, "games/move_sprite", "move_sprite");
    const std::string input = text_of(root() + "/games/move_sprite/input.json");
    const ge::tools::ExportTarget* windows = ge::tools::find_export_target("windows");
    REQUIRE(windows != nullptr);
    const std::string readme = ge::tools::make_readme(summary, input, *windows, "Move Sprite.exe");
    CHECK(readme.starts_with("Move Sprite 0.1.0\n================="));
    CHECK(has(readme, "double-click \"Move Sprite.exe\""));
    CHECK(has(readme, "Windows protected your PC"));
    CHECK(has(readme, "%APPDATA%\\ge\\move_sprite\\"));
    CHECK(has(readme, "move x: Right, D, Left, A"));
    CHECK(has(readme, "Escape"));
    CHECK(has(readme, "LICENSES.txt"));

    const std::string linux_readme =
        ge::tools::make_readme(summary, "", *ge::tools::find_export_target("linux"), "Move Sprite");
    CHECK(has(linux_readme, "./Move Sprite"));
    CHECK(has(linux_readme, "QUITTING"));
    CHECK(!has(linux_readme, "CONTROLS"));
    CHECK(has(ge::tools::make_readme(summary, "{ broken", *windows, "x.exe"), "QUITTING"));
    const std::string pointer = R"({"bindings": [
        {"action": "pointer_x", "sources": {"mouse:x": 1}},
        {"action": "click", "sources": {"mouse:left": 1}},
        {"sources": {"key:a": 1}},
    ]})";
    const std::string with_pointer = ge::tools::make_readme(summary, pointer, *windows, "x.exe");
    CHECK(!has(with_pointer, "pointer x"));
    CHECK(has(with_pointer, "click: Mouse left"));
}

TEST_CASE("export: the licenses of what the program contains") {
    const std::string text = ge::tools::make_licenses("MIT text", "zlib text");
    CHECK(has(text, "MIT text"));
    CHECK(has(text, "zlib text"));
    CHECK(has(text, "stb_image"));
    const std::string fallback = ge::tools::make_licenses("", "");
    CHECK(has(fallback, "MIT License"));
    CHECK(has(fallback, "zlib License"));
}

TEST_CASE("export: the name of a package") {
    const auto loaded = load_real("move_sprite");
    const auto summary = ge::game::summarize(loaded, "games/move_sprite", "move_sprite");
    CHECK(ge::tools::package_name(summary, *ge::tools::find_export_target("windows")) ==
          "Move_Sprite-0.1.0-windows");
    auto unversioned = summary;
    unversioned.version.clear();
    CHECK(ge::tools::package_name(unversioned, *ge::tools::find_export_target("linux")) ==
          "Move_Sprite-linux");
}

TEST_CASE("package: a game needs its modules and theirs") {
    const auto loaded = load_real("doodle_jump");
    REQUIRE(loaded.ok());
    const auto catalog = ge::game::catalog_of(ge::runner::linked_modules());
    const auto modules = ge::game::required_modules(loaded.game.manifest, catalog);
    CHECK(std::ranges::is_sorted(modules));
    for (const std::string& declared : loaded.game.manifest.modules) {
        CHECK(contains(modules, declared));
    }
    CHECK(modules.size() >= loaded.game.manifest.modules.size());
    for (const std::string& name : modules) {
        CHECK(catalog.find_module(name) != nullptr);
    }
}

TEST_CASE("package: the files a game reads, and only those") {
    const auto loaded = load_real("doodle_jump");
    REQUIRE(loaded.ok());
    const std::string manifest = root() + "/games/doodle_jump/game.json";
    const auto files = ge::game::package_files(loaded, manifest);
    CHECK(std::ranges::is_sorted(files));
    CHECK(contains(files, "art/doodle.png"));
    CHECK(contains(files, "art/doodle.json"));
    CHECK(contains(files, "input.json"));
    CHECK(std::ranges::none_of(files, [](const std::string& file) {
        return file.starts_with("scenarios/") || file.starts_with("tools/") || file == "game.json";
    }));
    for (const std::string& file : files) {
        CHECK(fs::is_regular_file(fs::path(root()) / "games" / "doodle_jump" / file));
    }
}

TEST_CASE("package: the manifest shipped has no scenarios") {
    std::string error;
    const std::string shipped = ge::game::package_manifest(
        R"({"schema_version": 1, "name": "g", "scenarios": ["a.json"], "modules": [],})", error);
    CHECK(error.empty());
    CHECK(has(shipped, "\"name\": \"g\""));
    CHECK(!has(shipped, "scenarios"));
    CHECK(ge::game::package_manifest("[1]", error).empty());
    CHECK(!error.empty());
    CHECK(ge::game::package_manifest("{ nope", error).empty());
}

#ifdef __linux__
TEST_CASE("export: the game's files are staged for any target") {
    // The step every target shares: what the player reads, and a manifest without scenarios.
    const Scratch scratch("stage");
    ge::tools::ExportRequest request;
    request.manifest = root() + "/games/move_sprite/game.json";
    ge::tools::ExportJob job;
    job.request = &request;
    job.loaded = load_real("move_sprite");
    REQUIRE(job.loaded.ok());
    job.folder = root() + "/games/move_sprite/";
    std::ostringstream err;
    CHECK(ge::tools::stage_game_files(job, scratch.path() / "data", err));
    CHECK(err.str().empty());
    CHECK(fs::exists(scratch.path() / "data" / "game.json"));
    CHECK(fs::exists(scratch.path() / "data" / "input.json"));
    CHECK(!fs::exists(scratch.path() / "data" / "scenarios"));

    // A manifest that cannot be read is said, not skipped.
    request.manifest = root() + "/games/move_sprite/nothing.json";
    std::ostringstream missing;
    CHECK(!ge::tools::stage_game_files(job, scratch.path() / "again", missing));
    CHECK(!missing.str().empty());
}

TEST_CASE("export: the commands that build and zip a package") {
    const Scratch scratch("commands");
    const std::string manifest = copy_game(scratch, "move_sprite");
    FakeSystem system;
    std::ostringstream out;
    std::ostringstream err;
    const ExportRequest request = request_in(scratch, manifest);
    REQUIRE(ge::tools::export_game(request, out, err, system.runner()) == 0);
    CHECK(err.str().empty());

    REQUIRE(system.commands.size() == 3);
    const auto& configure = system.commands[0];
    CHECK(configure.front() == "cmake");
    CHECK(contains(configure, "-DGE_BUILD_PLATFORM=ON"));
    CHECK(contains(configure, "-DGE_PLAYER_MODULES=action_control;debug_draw;transform2d"));
    CHECK(contains(configure, "-DCMAKE_BUILD_TYPE=Release"));
    CHECK(system.commands[1][1] == "--build");
    CHECK(contains(system.commands[1], "ge_player"));
    CHECK(contains(system.commands[2], "--format=zip"));
    CHECK(contains(system.commands[2], "Move_Sprite-0.1.0-linux.zip"));
    CHECK(contains(system.commands[2], "Move_Sprite-0.1.0-linux"));

    const fs::path package = scratch.path() / "dist" / "Move_Sprite-0.1.0-linux";
    CHECK(fs::is_regular_file(package / "Move Sprite"));
    CHECK(fs::is_regular_file(package / "README.txt"));
    CHECK(fs::is_regular_file(package / "LICENSES.txt"));
    CHECK(fs::is_regular_file(package / "data" / "game.json"));
    CHECK(fs::is_regular_file(package / "data" / "input.json"));
    CHECK(fs::is_regular_file(package / "data" / "scenes" / "main.json"));
    CHECK(!fs::exists(package / "data" / "scenarios"));
    CHECK(!has(text_of(package / "data" / "game.json"), "scenarios"));
    CHECK(has(text_of(package / "LICENSES.txt"), "MIT License"));
    CHECK(has(out.str(), "3 scenarios pass"));
}

TEST_CASE("export: extra CMake arguments reach the configure step") {
    const Scratch scratch("extra");
    ExportRequest request = request_in(scratch, copy_game(scratch, "move_sprite"));
    request.cmake_arguments = {"-DFETCHCONTENT_SOURCE_DIR_SDL3=/somewhere"};
    FakeSystem system;
    std::ostringstream out;
    std::ostringstream err;
    REQUIRE(ge::tools::export_game(request, out, err, system.runner()) == 0);
    CHECK(contains(system.commands[0], "-DFETCHCONTENT_SOURCE_DIR_SDL3=/somewhere"));
}

TEST_CASE("export: a game whose tests fail is not exported") {
    const Scratch scratch("failing");
    const std::string manifest = copy_game(scratch, "move_sprite");
    const fs::path scenario = fs::path(manifest).parent_path() / "scenarios" / "idle.json";
    std::string text = text_of(scenario);
    const std::size_t at = text.find("== 400");
    REQUIRE(at != std::string::npos);
    text.replace(at, 6, "== 399");
    std::ofstream(scenario, std::ios::trunc) << text;

    FakeSystem system;
    std::ostringstream out;
    std::ostringstream err;
    ExportRequest request = request_in(scratch, manifest);
    CHECK(ge::tools::export_game(request, out, err, system.runner()) == 1);
    CHECK(system.commands.empty());

    CHECK(has(err.str(), "idle"));
    CHECK(has(err.str(), "nothing was exported"));

    request.skip_tests = true;
    std::ostringstream out2;
    std::ostringstream err2;
    CHECK(ge::tools::export_game(request, out2, err2, system.runner()) == 0);
    CHECK(!has(out2.str(), "scenarios pass"));
}

TEST_CASE("export: a step that fails stops the export") {
    for (int step = 0; step < 3; ++step) {
        const Scratch scratch("step" + std::to_string(step));
        FakeSystem system;
        system.fail_at = step;
        std::ostringstream out;
        std::ostringstream err;
        CHECK(ge::tools::export_game(request_in(scratch, copy_game(scratch, "move_sprite")), out,
                                     err, system.runner()) == 1);
        CHECK(!err.str().empty());
        CHECK(system.commands.size() == static_cast<std::size_t>(step + 1));
    }
}

TEST_CASE("export: a build that makes no program is reported") {
    const Scratch scratch("noplayer");
    FakeSystem system;
    system.make_player = false;
    std::ostringstream out;
    std::ostringstream err;
    CHECK(ge::tools::export_game(request_in(scratch, copy_game(scratch, "move_sprite")), out, err,
                                 system.runner()) == 1);
    CHECK(has(err.str(), "ge_player is not in"));
}

TEST_CASE("export: a game with an icon gets it as an .ico file") {
    const Scratch scratch("icon");
    const std::string manifest = copy_game(scratch, "move_sprite");
    const fs::path folder = fs::path(manifest).parent_path();
    std::string text = text_of(manifest);
    text.insert(text.find('{') + 1, R"("icon": "icon.png",)");
    std::ofstream(manifest, std::ios::trunc) << text;
    std::ofstream(folder / "icon.png", std::ios::binary) << png_of(32, 32);

    FakeSystem system;
    std::ostringstream out;
    std::ostringstream err;
    REQUIRE(ge::tools::export_game(request_in(scratch, manifest), out, err, system.runner()) == 0);
    CHECK(std::ranges::any_of(system.commands[0], [](const std::string& argument) {
        return argument.starts_with("-DGE_PLAYER_ICON=") && argument.ends_with("icon.ico");
    }));
    CHECK(fs::is_regular_file(scratch.path() / "build" / "icon.ico"));
    CHECK(fs::is_regular_file(scratch.path() / "dist" / "Move_Sprite-0.1.0-linux" / "data" /
                              "icon.png"));

    std::ofstream(folder / "icon.png", std::ios::binary | std::ios::trunc) << "not a png at all";
    std::ostringstream out2;
    std::ostringstream err2;
    FakeSystem other;
    CHECK(ge::tools::export_game(request_in(scratch, manifest), out2, err2, other.runner()) == 1);
    CHECK(has(err2.str(), "not a PNG"));
    CHECK(other.commands.empty());
}
#endif

TEST_CASE("export: what cannot be exported is said") {
    const Scratch scratch("refused");
    FakeSystem system;
    std::ostringstream out;
    std::ostringstream err;
    ExportRequest request = request_in(scratch, copy_game(scratch, "move_sprite"));
    request.target = "amiga";
    CHECK(ge::tools::export_game(request, out, err, system.runner()) == 1);
    CHECK(has(err.str(), "unknown target \"amiga\""));
    CHECK(has(err.str(), "windows"));

#ifndef _WIN32
    std::ostringstream err2;
    request.target = "windows";
    CHECK(ge::tools::export_game(request, out, err2, system.runner()) == 1);
    CHECK(has(err2.str(), "on Windows"));
#endif
    CHECK(system.commands.empty());

    // A game that is not valid is not exported either, on any system.
    request.target = "linux";
    request.manifest = (scratch.path() / "missing" / "game.json").generic_string();
    std::ostringstream err3;
    CHECK(ge::tools::export_game(request, out, err3, system.runner()) == 1);
    CHECK(system.commands.empty());
}

TEST_CASE("gectl: export is a command") {
    std::ostringstream out;
    std::ostringstream err;
    CHECK(ge::tools::run_gectl({"export", "--targets"}, out, err) == 0);
    CHECK(has(out.str(), "windows"));
    std::ostringstream err2;
    CHECK(ge::tools::run_gectl({"export"}, out, err2) == 2);
    CHECK(has(err2.str(), "exactly one game file"));
    std::ostringstream err3;
    CHECK(ge::tools::run_gectl({"export", "g.json", "--target"}, out, err3) == 2);
    std::ostringstream err4;
    CHECK(ge::tools::run_gectl({"export", "nope/game.json", "--target", "amiga", "--out", "x",
                                "--source", ".", "--build-dir", "b", "--cmake-arg", "-DX=1",
                                "--skip-tests"},
                               out, err4) == 1);
    CHECK(has(err4.str(), "unknown target"));
}
