#include "../support/mini_test.hpp"
#include "runner.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Run {
    int code = 0;
    std::string out;
    std::string err;
};

Run gerun(const std::vector<std::string>& args) {
    std::ostringstream out;
    std::ostringstream err;
    Run run;
    run.code = ge::runner::run_gerun(args, out, err);
    run.out = out.str();
    run.err = err.str();
    return run;
}

bool has(const std::string& text, const char* fragment) {
    return text.find(fragment) != std::string::npos;
}

const std::string sparks = std::string(GE_SOURCE_DIR) + "/games/sparks/game.json";
const std::string pilot = std::string(GE_SOURCE_DIR) + "/games/pilot/game.json";

/// A scratch game directory, removed when the test ends.
class Scratch {
public:
    explicit Scratch(const std::string& name)
        : root_(std::filesystem::temp_directory_path() / ("ge_runner_test_" + name)) {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
        std::filesystem::create_directories(root_, error);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    Scratch(Scratch&&) = delete;
    Scratch& operator=(Scratch&&) = delete;
    ~Scratch() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }
    void write(const std::string& name, const std::string& content) const {
        std::ofstream(root_ / name, std::ios::binary) << content;
    }
    [[nodiscard]] std::string path(const std::string& name) const {
        return (root_ / name).generic_string();
    }

private:
    std::filesystem::path root_;
};

const char* const tiny_game = R"({
    "schema_version": 1, "name": "tiny", "modules": ["transform2d"],
    "prefabs": [], "scenes": ["main.json"], "start_scene": "main" %s
})";

std::string tiny(const char* extra) {
    std::string text = tiny_game;
    text.replace(text.find("%s"), 2, extra);
    return text;
}

} // namespace

TEST_CASE("runner: a game made only of JSON files runs") {
    const Run run = gerun({sparks, "--ticks", "60"});
    CHECK(run.code == 0);
    CHECK(run.err.empty());
    CHECK(has(run.out, "sparks: 60 ticks, 4 entities, fingerprint "));
}

TEST_CASE("runner: entities live and die by the data alone") {
    // The embers have 90 ticks of life; the two drifters never expire.
    CHECK(has(gerun({sparks, "--ticks", "89"}).out, "4 entities"));
    CHECK(has(gerun({sparks, "--ticks", "120"}).out, "2 entities"));
}

TEST_CASE("runner: the same game gives the same state, a longer run another") {
    const Run first = gerun({sparks, "--ticks", "100"});
    const Run second = gerun({sparks, "--ticks", "100"});
    const Run longer = gerun({sparks, "--ticks", "101"});
    CHECK(first.out == second.out);
    CHECK(first.out != longer.out);
}

TEST_CASE("runner: usage problems") {
    CHECK(gerun({}).code == 2);
    CHECK(has(gerun({}).err, "no game file given"));
    CHECK(gerun({sparks, "--ticks"}).code == 2);
    CHECK(gerun({sparks, "--ticks", "ten"}).code == 2);
    CHECK(has(gerun({sparks, "--ticks", "10x"}).err, "whole number"));
    CHECK(gerun({sparks, "--speed"}).code == 2);
    CHECK(has(gerun({sparks, sparks}).err, "only one game file"));
}

TEST_CASE("runner: an invalid game is reported with its problems") {
    const Run missing = gerun({"/definitely/not/here/game.json"});
    CHECK(missing.code == 1);
    CHECK(has(missing.err, "cannot read the file"));
    CHECK(missing.out.empty());
}

TEST_CASE("runner: --test runs the scenarios of the reference games") {
    const Run sparks_run = gerun({sparks, "--test"});
    CHECK(sparks_run.code == 0);
    CHECK(has(sparks_run.out, "PASS sparks/embers_burn_out (120 ticks)"));
    CHECK(has(sparks_run.out, "2 passed, 0 failed"));
    const Run pilot_run = gerun({pilot, "--test"});
    CHECK(pilot_run.code == 0);
    CHECK(has(pilot_run.out, "PASS pilot/fly_right_then_stop (180 ticks)"));
    CHECK(has(pilot_run.out, "3 passed, 0 failed"));
}

TEST_CASE("runner: --scenario picks one") {
    const Run one = gerun({pilot, "--scenario", "idle"});
    CHECK(one.code == 0);
    CHECK(has(one.out, "PASS pilot/idle"));
    CHECK(has(one.out, "1 passed, 0 failed"));
    CHECK(!has(one.out, "wraps"));
    const Run unknown = gerun({pilot, "--scenario", "nope"});
    CHECK(unknown.code == 1);
    CHECK(has(unknown.err, "no scenario named \"nope\""));
    CHECK(gerun({pilot, "--scenario"}).code == 2);
}

TEST_CASE("runner: a failing scenario is reported and fails the run") {
    const Scratch scratch("failing");
    scratch.write("game.json", R"({
        "schema_version": 1, "name": "tiny", "modules": ["transform2d"],
        "scenes": ["main.json"], "scenarios": ["check.json"], "start_scene": "main"
    })");
    scratch.write("main.json", R"({"schema_version": 1, "name": "main", "entities": [
        {"name": "ball", "components": {"transform2d.Position": {"x": 1}}}]})");
    scratch.write("check.json", R"({"schema_version": 1, "name": "ball_moves", "ticks": 10,
        "expect": ["ball.transform2d.Position.x == 5"]})");
    const Run run = gerun({scratch.path("game.json"), "--test"});
    CHECK(run.code == 1);
    CHECK(has(run.out, "FAIL tiny/ball_moves (10 ticks)"));
    CHECK(has(run.out, "\"ball.transform2d.Position.x == 5\" is false (the value is 1)"));
    CHECK(has(run.out, "0 passed, 1 failed"));
}

TEST_CASE("runner: a game without scenarios has nothing to test") {
    const Scratch scratch("none");
    scratch.write("game.json", tiny(""));
    scratch.write("main.json", R"({"schema_version": 1, "name": "main", "entities": [
        {"components": {"transform2d.Position": {}}}]})");
    const Run run = gerun({scratch.path("game.json"), "--test"});
    CHECK(run.code == 0);
    CHECK(has(run.out, "tiny: no scenarios"));
}

TEST_CASE("runner: a scenario that does not match the game stops the run") {
    const Scratch scratch("invalid");
    scratch.write("game.json", tiny(R"(, "scenarios": ["s.json"])"));
    scratch.write("main.json", R"({"schema_version": 1, "name": "main", "entities": [
        {"name": "ball", "components": {"transform2d.Position": {}}}]})");
    scratch.write("s.json", R"({"schema_version": 1, "name": "s", "ticks": 5,
        "expect": ["bal gone"]})");
    const Run run = gerun({scratch.path("game.json"), "--test"});
    CHECK(run.code == 1);
    CHECK(has(run.err, "no entity named \"bal\" (did you mean \"ball\"?)"));
}

TEST_CASE("runner: the scenarios of every game of the repository pass") {
    int games = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator(std::string(GE_SOURCE_DIR) + "/games")) {
        const std::string manifest = (entry.path() / "game.json").generic_string();
        if (!std::filesystem::exists(manifest)) {
            continue;
        }
        ++games;
        const Run run = gerun({manifest, "--test"});
        CHECK(run.err.empty());
        CHECK(run.code == 0);
        if (run.code != 0) {
            std::fputs(run.out.c_str(), stderr);
        }
    }
    CHECK(games >= 2);
}

TEST_CASE("runner: the launcher is a game whose scenarios pass") {
    const Run run = gerun({std::string(GE_SOURCE_DIR) + "/tools/launcher/game.json", "--test"});
    CHECK(run.err.empty());
    CHECK(run.code == 0);
    if (run.code != 0) {
        std::fputs(run.out.c_str(), stderr);
    }
}
