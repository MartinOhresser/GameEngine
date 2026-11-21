// The program of an exported game: it plays the game of its data folder and nothing else.

#include "ge/platform/sdl/player.hpp"

#include "mini_test.hpp"
#include "registry.hpp"

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace sdl = ge::platform::sdl;
namespace fs = std::filesystem;

namespace {

struct Run {
    int code = 0;
    std::string out;
    std::string err;
};

Run player(const std::vector<std::string>& args) {
    std::ostringstream out;
    std::ostringstream err;
    Run run;
    run.code = sdl::run_player(args, ge::runner::linked_modules(), out, err);
    run.out = out.str();
    run.err = err.str();
    return run;
}

std::string move_sprite() {
    return std::string(GE_SOURCE_DIR) + "/games/move_sprite";
}

bool has(const std::string& text, const char* fragment) {
    return text.find(fragment) != std::string::npos;
}

} // namespace

TEST_CASE("player: plays the game of its data folder, with the processor if asked") {
    for (const bool software : {false, true}) {
        std::vector<std::string> args{"--data", move_sprite(), "--headless", "--frames", "5"};
        if (software) {
            args.emplace_back("--software");
        }
        const Run run = player(args);
        CHECK(run.code == 0);
        CHECK(run.err.empty());
        CHECK(has(run.out, "move_sprite: 5 frames"));
    }
}

TEST_CASE("player: saves a screenshot when asked") {
    const fs::path shot = fs::temp_directory_path() / "ge_player_test.bmp";
    const Run run = player({"--data", move_sprite() + "/", "--headless", "--software", "--frames",
                            "3", "--screenshot", shot.string()});
    CHECK(run.code == 0);
    CHECK(fs::exists(shot));
    std::error_code ignored;
    fs::remove(shot, ignored);
    const Run bad = player({"--data", move_sprite(), "--headless", "--frames", "2", "--screenshot",
                            "/nonexistent-dir/shot.bmp"});
    CHECK(bad.code == 1);
}

TEST_CASE("player: says what is wrong instead of opening a window") {
    const Run missing = player({"--data", "/nonexistent/data", "--headless"});
    CHECK(missing.code == 1);
    CHECK(has(missing.err, "could not be read"));
    const Run unknown = player({"--fly"});
    CHECK(unknown.code == 2);
    CHECK(has(unknown.err, "unknown argument --fly"));
    CHECK(player({"--frames"}).code == 2);
    const Run words = player({"--frames", "many"});
    CHECK(words.code == 2);
    CHECK(has(words.err, "whole number"));
}
