#include "ge/json/json.hpp"

#include "../support/mini_test.hpp"
#include "gectl.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Run {
    int code = 0;
    std::string out;
    std::string err;
};

Run gectl(const std::vector<std::string>& args) {
    std::ostringstream out;
    std::ostringstream err;
    Run run;
    run.code = ge::tools::run_gectl(args, out, err);
    run.out = out.str();
    run.err = err.str();
    return run;
}

bool has(const std::string& text, const char* fragment) {
    return text.find(fragment) != std::string::npos;
}

/// A scratch modules directory, removed when the test ends.
class Sandbox {
public:
    explicit Sandbox(const std::string& name)
        : root_(fs::temp_directory_path() / ("ge_gectl_test_" + name)) {
        std::error_code error;
        fs::remove_all(root_, error);
        fs::create_directories(root_, error);
    }
    Sandbox(const Sandbox&) = delete;
    Sandbox& operator=(const Sandbox&) = delete;
    Sandbox(Sandbox&&) = delete;
    Sandbox& operator=(Sandbox&&) = delete;
    ~Sandbox() {
        std::error_code error;
        fs::remove_all(root_, error);
    }

    [[nodiscard]] std::string dir() const { return root_.generic_string(); }

    void write(const std::string& relative, const std::string& content) const {
        const fs::path path = root_ / relative;
        std::error_code error;
        fs::create_directories(path.parent_path(), error);
        std::ofstream(path, std::ios::binary) << content;
    }

    [[nodiscard]] std::string read(const std::string& relative) const {
        const std::ifstream stream(root_ / relative, std::ios::binary);
        std::ostringstream text;
        text << stream.rdbuf();
        return text.str();
    }

private:
    fs::path root_;
};

std::string module_json(const std::string& name, const std::string& depends = "") {
    return R"({"schema_version":1,"name":")" + name + R"(","version":"1.0.0","depends":[)" +
           depends +
           R"(],"components":[{"name":"C","replicable":true,"fields":[{"name":"x","type":"f32"}]}],
           "systems":[{"name":"s","stage":"Update","writes":["C"]}]})";
}

} // namespace

TEST_CASE("gectl: the repository's modules are valid, catalogued and up to date") {
    const std::string modules = std::string(GE_SOURCE_DIR) + "/modules";
    const Run check = gectl({"check", "--modules", modules});
    CHECK(check.code == 0);
    CHECK(has(check.out, "ok: 3 modules"));
    const Run generated = gectl({"generate", "--check", "--modules", modules});
    CHECK(generated.code == 0);
    if (generated.code != 0) {
        std::fputs(generated.err.c_str(), stderr);
    }
    const Run catalog = gectl({"catalog", "--modules", modules});
    CHECK(catalog.code == 0);
    const auto parsed = ge::json::parse(catalog.out);
    REQUIRE(parsed.ok());
    CHECK(parsed.value.find("engine")->find("stages")->items().size() == 5U);
    CHECK(parsed.value.find("modules")->items().size() == 3U);
}

TEST_CASE("gectl: help and usage errors") {
    CHECK(gectl({"help"}).code == 0);
    CHECK(has(gectl({"--help"}).out, "Usage:"));
    CHECK(gectl({"-h"}).code == 0);
    const Run none = gectl({});
    CHECK(none.code == 2);
    CHECK(has(none.err, "Usage:"));
    const Run unknown = gectl({"frobnicate"});
    CHECK(unknown.code == 2);
    CHECK(has(unknown.err, "unknown command \"frobnicate\""));
    const Run bad_option = gectl({"check", "--bogus"});
    CHECK(bad_option.code == 2);
    CHECK(has(bad_option.err, "unknown option --bogus"));
    const Run no_dir = gectl({"check", "--modules"});
    CHECK(no_dir.code == 2);
    CHECK(has(no_dir.err, "--modules needs a directory"));
}

TEST_CASE("gectl: check reports every problem with its file and place") {
    const Sandbox sandbox("check");
    sandbox.write("good/module.json", module_json("good"));
    sandbox.write("bad/module.json", R"({"schema_version":1,"name":"bad","version":"x"})");
    sandbox.write("misnamed/module.json", module_json("other"));
    sandbox.write("empty_dir/placeholder.txt", "");
    const Run run = gectl({"check", "--modules", sandbox.dir()});
    CHECK(run.code == 1);
    CHECK(has(run.err, "bad/module.json: version: \"x\" is not a version"));
    CHECK(has(run.err, "lives in the directory \"misnamed\""));
    CHECK(has(run.err, "empty_dir/module.json: cannot read module.json"));
    CHECK(has(run.err, "3 problems found"));
}

TEST_CASE("gectl: check also looks across modules") {
    const Sandbox sandbox("cross");
    sandbox.write("a/module.json", module_json("a", "\"nowhere\""));
    const Run run = gectl({"check", "--modules", sandbox.dir()});
    CHECK(run.code == 1);
    CHECK(has(run.err, "unknown module \"nowhere\""));
    CHECK(has(run.err, "1 problem found"));
}

TEST_CASE("gectl: a missing modules directory is an error") {
    const Run run = gectl({"check", "--modules", "/definitely/not/here"});
    CHECK(run.code == 1);
    CHECK(has(run.err, "cannot read the modules directory"));
}

TEST_CASE("gectl: generate writes the files, then finds them up to date") {
    const Sandbox sandbox("generate");
    sandbox.write("a/module.json", module_json("a"));
    sandbox.write("b/module.json", module_json("b", "\"a\""));

    const Run stale = gectl({"generate", "--check", "--modules", sandbox.dir()});
    CHECK(stale.code == 1);
    CHECK(has(stale.err, "a/gen/a.gen.cpp: out of date"));

    const Run first = gectl({"generate", "--modules", sandbox.dir()});
    CHECK(first.code == 0);
    CHECK(has(first.out, "wrote "));
    CHECK(has(sandbox.read("a/gen/ge/mod/a/a.gen.hpp"), "namespace ge::mod::a {"));
    CHECK(has(sandbox.read("b/gen/b.gen.cpp"), "\"a\""));

    const Run again = gectl({"generate", "--modules", sandbox.dir()});
    CHECK(again.code == 0);
    CHECK(again.out.empty());
    CHECK(gectl({"generate", "--check", "--modules", sandbox.dir()}).code == 0);

    sandbox.write("a/gen/a.gen.cpp", "// edited by hand\n");
    CHECK(gectl({"generate", "--check", "--modules", sandbox.dir()}).code == 1);
}

TEST_CASE("gectl: generate can target some modules") {
    const Sandbox sandbox("targets");
    sandbox.write("a/module.json", module_json("a"));
    sandbox.write("b/module.json", module_json("b"));
    CHECK(gectl({"generate", "b", "--modules", sandbox.dir()}).code == 0);
    CHECK(!sandbox.read("b/gen/b.gen.cpp").empty());
    CHECK(sandbox.read("a/gen/a.gen.cpp").empty());
    const Run unknown = gectl({"generate", "zzz", "--modules", sandbox.dir()});
    CHECK(unknown.code == 2);
    CHECK(has(unknown.err, "no module named \"zzz\""));
    const Run invalid = gectl({"generate", "--modules", "/definitely/not/here"});
    CHECK(invalid.code == 1);
}

TEST_CASE("gectl: catalog output is compact on request") {
    const Sandbox sandbox("catalog");
    sandbox.write("a/module.json", module_json("a"));
    const Run pretty = gectl({"catalog", "--modules", sandbox.dir()});
    const Run compact = gectl({"catalog", "--compact", "--modules", sandbox.dir()});
    CHECK(pretty.code == 0);
    CHECK(compact.code == 0);
    CHECK(has(pretty.out, "\n  \"engine\""));
    CHECK(compact.out.find('\n') == compact.out.size() - 1U);
    CHECK(gectl({"catalog", "--modules", "/definitely/not/here"}).code == 1);
}

TEST_CASE("gectl: validate checks a game's files against the modules") {
    const Sandbox sandbox("validate");
    sandbox.write("modules/a/module.json", module_json("a"));
    const std::string modules = sandbox.dir() + "/modules";
    sandbox.write("game/game.json", R"({
        "schema_version": 1, "name": "demo", "modules": ["a"],
        "prefabs": ["p.json"], "scenes": ["s.json"], "start_scene": "main"
    })");
    sandbox.write("game/p.json",
                  R"({"schema_version": 1, "name": "p", "components": {"a.C": {"x": 1}}})");
    sandbox.write("game/s.json",
                  R"({"schema_version": 1, "name": "main", "entities": [{"prefab": "p"}]})");
    const std::string game = sandbox.dir() + "/game/game.json";

    const Run good = gectl({"validate", "--modules", modules, game});
    CHECK(good.code == 0);
    CHECK(has(good.out, "valid (1 prefabs, 1 scenes)"));

    sandbox.write("game/p.json",
                  R"({"schema_version": 1, "name": "p", "components": {"a.C": {"y": 1}}})");
    const Run bad = gectl({"validate", "--modules", modules, game});
    CHECK(bad.code == 1);
    CHECK(has(bad.err, "p.json: components.a.C.y: unknown field"));

    CHECK(gectl({"validate", "--modules", modules}).code == 2);
    CHECK(gectl({"validate", "--modules", modules, "a.json", "b.json"}).code == 2);
    CHECK(gectl({"validate", "--modules", modules, sandbox.dir() + "/nope.json"}).code == 1);
    CHECK(gectl({"validate", "--modules", "/definitely/not/here", game}).code == 1);
}
