// The launcher's export service: `gectl export` in a thread, with a fake in place of CMake.

#include "ge/platform/sdl/tool_export.hpp"

#include "mini_test.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
namespace sdl = ge::platform::sdl;

namespace {

const std::string root = GE_SOURCE_DIR;

bool has_line(const std::vector<std::string>& lines, const std::string& fragment) {
    return std::ranges::any_of(lines, [&fragment](const std::string& line) {
        return line.find(fragment) != std::string::npos;
    });
}

/// Polls until the export has ended (or about ten seconds went by), collecting its lines.
sdl::ExportProgress wait_for_end(sdl::ExportService& service, std::vector<std::string>& lines) {
    sdl::ExportProgress progress;
    for (int i = 0; i < 2000; ++i) {
        progress = service.poll();
        lines.insert(lines.end(), progress.lines.begin(), progress.lines.end());
        if (progress.finished) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return progress;
}

} // namespace

#ifdef __linux__
TEST_CASE("tool export: the platforms are those of gectl, and an export runs in the background") {
    const fs::path scratch = fs::temp_directory_path() / "ge_tool_export_test";
    std::error_code error;
    fs::remove_all(scratch, error);
    fs::create_directories(scratch / "games", error);
    fs::copy(fs::path(root) / "games" / "move_sprite", scratch / "games" / "move_sprite",
             fs::copy_options::recursive, error);

    std::atomic<bool> release{false};
    std::atomic<int> commands{0};
    sdl::ToolExportOptions options;
    options.source_dir = (scratch / "engine").generic_string();
    options.dist_dir = (scratch / "dist").generic_string();
    options.include_hidden = true;
    options.runner = [&](const std::vector<std::string>& command) {
        ++commands;
        while (!release) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (command.size() > 2 && command[1] == "--build") {
            const fs::path player = fs::path(command[2]) / "platform" / "sdl" / "ge_player";
            std::error_code ignored;
            fs::create_directories(player.parent_path(), ignored);
            std::ofstream(player) << "program";
        }
        return 0;
    };
    const std::unique_ptr<sdl::ExportService> service = sdl::make_tool_export(options);
    const std::vector<std::string> targets = service->targets();
    REQUIRE(targets.size() == 2U);
    CHECK(targets[0] == "Windows (64-bit)");
    CHECK(targets[1] == "Linux (64-bit)");

    std::string why;
    CHECK(!service->start("x/game.json", 9, why));
    CHECK(!why.empty());

    const std::string manifest = (scratch / "games" / "move_sprite" / "game.json").generic_string();
    REQUIRE(service->start(manifest, 1, why));
    // The export waits for the fake CMake: the service says it is running, and refuses another.
    CHECK(!service->start(manifest, 1, why));
    CHECK(why == "An export is already going on.");
    CHECK(service->poll().running);

    release = true;
    std::vector<std::string> lines;
    const sdl::ExportProgress end = wait_for_end(*service, lines);
    CHECK(end.finished);
    CHECK(end.succeeded);
    CHECK(!end.running);
    CHECK(has_line(lines, "Exporting Move Sprite for Linux"));
    CHECK(has_line(lines, "Done:"));
    CHECK(commands == 3);
    CHECK(fs::is_regular_file(scratch / "dist" / "Move_Sprite-0.1.0-linux.zip") ||
          fs::is_directory(scratch / "dist" / "Move_Sprite-0.1.0-linux"));
    CHECK(!service->poll().finished); // told once

    // A failure is reported as one.
    options.runner = [](const std::vector<std::string>&) { return 1; };
    const std::unique_ptr<sdl::ExportService> failing = sdl::make_tool_export(options);
    REQUIRE(failing->start(manifest, 1, why));
    lines.clear();
    const sdl::ExportProgress failed = wait_for_end(*failing, lines);
    CHECK(failed.finished);
    CHECK(!failed.succeeded);
    CHECK(has_line(lines, "could not prepare the build"));
    fs::remove_all(scratch, error);
}

TEST_CASE("tool export: the output of a command is read line by line") {
    std::vector<std::string> lines;
    const int code =
        ge::tools::run_process_lines({"sh", "-c", "echo one; echo two 1>&2; printf last; exit 3"},
                                     [&lines](const std::string& line) { lines.push_back(line); });
    CHECK(code == 1);
    REQUIRE(lines.size() == 3U);
    CHECK(lines[0] == "one");
    CHECK(lines[1] == "two");
    CHECK(lines[2] == "last");
    CHECK(ge::tools::run_process_lines({"sh", "-c", "exit 0"}, [](const std::string&) {}) == 0);
}
#endif

TEST_CASE("tool export: a service for the platforms people are offered") {
    const sdl::ToolExportOptions options;
    const std::unique_ptr<sdl::ExportService> service = sdl::make_tool_export(options);
    CHECK(service->targets() == std::vector<std::string>{"Windows (64-bit)"});
    CHECK(!service->poll().running);
}
