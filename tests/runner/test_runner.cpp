#include "../support/mini_test.hpp"
#include "runner.hpp"

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
