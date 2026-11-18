// The launcher's host, driven by a scripted backend: no window, no SDL.

#include "ge/platform/backend.hpp"
#include "ge/platform/sdl/launcher.hpp"

#include "mini_test.hpp"
#include "registry.hpp"

#include <algorithm>
#include <sstream>
#include <string>

namespace sdl = ge::platform::sdl;

namespace {

const std::string root = GE_SOURCE_DIR;
const std::string launcher_path = root + "/tools/launcher/game.json";

ge::game::LoadedGame launcher() {
    return ge::game::load_game(launcher_path, ge::game::disk_reader(),
                               ge::game::catalog_of(ge::runner::linked_modules()));
}

struct Run {
    sdl::LauncherResult result;
    std::string err;
};

/// Runs the launcher of the repository on the games of the repository.
Run run_launcher_on(ge::platform::ScriptedBackend& backend) {
    const ge::game::LoadedGame loaded = launcher();
    CHECK(loaded.ok());
    sdl::LauncherOptions options;
    options.games_dir = root + "/games";
    std::ostringstream out;
    std::ostringstream err;
    Run done;
    done.result = sdl::run_launcher(backend, loaded, launcher_path, ge::runner::linked_modules(),
                                    options, out, err);
    done.err = err.str();
    return done;
}

void press(ge::platform::ScriptedBackend& backend, std::uint64_t frame, const char* key) {
    backend.at(frame, key, 1.0F);
    backend.at(frame + 1, key, 0.0F);
}

ge::game::GameSummary summary_of(const std::string& description) {
    ge::game::GameSummary summary;
    summary.title = "Demo";
    summary.version = "1.2.3";
    summary.description = description;
    summary.window_width = 480;
    summary.window_height = 720;
    summary.modules = {"a", "b", "c"};
    summary.valid = true;
    return summary;
}

} // namespace

TEST_CASE("launcher: the lines about a game have a fixed count and fit the panel") {
    const auto lines = sdl::detail_lines(summary_of(
        "Left and right steer the jumper, which bounces by itself on every platform it lands on."));
    REQUIRE(lines.size() == sdl::detail_line_count);
    CHECK(lines[0] == "Demo");
    CHECK(lines[1] == "Version 1.2.3");
    CHECK(lines[2] == "Left and right steer the jumper,");
    CHECK(lines[3] == "which bounces by itself on every");
    CHECK(lines[4] == "platform it lands on.");
    CHECK(lines[5].empty());
    CHECK(lines[8] == "480 x 720 pixels");
    CHECK(lines[9] == "3 modules");
}

TEST_CASE("launcher: a long description is cut with an ellipsis, an invalid game says why") {
    std::string long_text;
    for (int i = 0; i < 40; ++i) {
        long_text += "word" + std::to_string(i) + " ";
    }
    const auto lines = sdl::detail_lines(summary_of(long_text));
    REQUIRE(lines.size() == sdl::detail_line_count);
    CHECK(!lines[7].empty());
    CHECK(lines[7].ends_with("\xE2\x80\xA6"));

    ge::game::GameSummary broken = summary_of("fine");
    broken.valid = false;
    broken.problems = {"game.json: the prefab \"x\" is missing"};
    const auto invalid = sdl::detail_lines(broken);
    CHECK(invalid[2] == "game.json: the prefab \"x\" is");
}

TEST_CASE(
    "launcher: Enter plays the chosen game on the same window, Escape brings the launcher back") {
    ge::platform::ScriptedBackend backend(60, 400);
    press(backend, 5, "key:down"); // the second game of the folder
    press(backend, 10, "key:return");
    backend.at(100, "key:escape", 1.0F); // leaves the game: back to the launcher
    backend.at(101, "key:escape", 0.0F);
    backend.at(150, "key:escape", 1.0F); // leaves the launcher
    const Run done = run_launcher_on(backend);
    CHECK(done.err.empty());
    REQUIRE(done.result.started);
    REQUIRE(done.result.played.size() == 1U);
    CHECK(done.result.played[0] == "doodle_jump");
    // The launcher's window is back, with fresh assets each time (launcher, game, launcher).
    CHECK(backend.width() == 800);
    CHECK(backend.height() == 560);
    CHECK(backend.resets() == 3);
}

TEST_CASE("launcher: the game takes the window size it asks for") {
    ge::platform::ScriptedBackend backend(60, 60);
    press(backend, 5, "key:down");
    press(backend, 10, "key:return");
    const Run done = run_launcher_on(backend); // the window is closed while the game plays
    REQUIRE(done.result.played.size() == 1U);
    CHECK(backend.width() == 480); // doodle_jump's window, not the launcher's
    CHECK(backend.height() == 720);
}

TEST_CASE("launcher: Escape in the launcher leaves it, and nothing is played") {
    ge::platform::ScriptedBackend backend(60, 100);
    backend.at(5, "key:escape", 1.0F);
    const Run done = run_launcher_on(backend);
    CHECK(done.result.started);
    CHECK(done.result.played.empty());
    CHECK(backend.width() == 800);
}

TEST_CASE("launcher: a launcher that is not a game of this program does not start") {
    ge::platform::ScriptedBackend backend(60, 10);
    ge::game::LoadedGame empty;
    sdl::LauncherOptions options;
    options.games_dir = root + "/games";
    std::ostringstream out;
    std::ostringstream err;
    const auto result = sdl::run_launcher(backend, empty, "nowhere/game.json",
                                          ge::runner::linked_modules(), options, out, err);
    CHECK(!result.started);
    CHECK(!err.str().empty());
}
