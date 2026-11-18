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
    CHECK(has(check.out, "ok: "));
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
    CHECK(parsed.value.find("modules")->items().size() >= 4U);
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

TEST_CASE("gectl: list shows the games of a directory, the invalid ones too") {
    const Sandbox sandbox("list");
    sandbox.write("modules/a/module.json", module_json("a"));
    const std::string modules = sandbox.dir() + "/modules";
    const std::string games = sandbox.dir() + "/games";
    sandbox.write("games/beta/game.json", R"({
        "schema_version": 1, "name": "beta", "title": "Beta Game", "version": "2.0.1",
        "modules": ["a"], "window": {"width": 480, "height": 720},
        "prefabs": [], "scenes": ["s.json"], "start_scene": "main"
    })");
    sandbox.write("games/beta/s.json", R"({"schema_version": 1, "name": "main", "entities": []})");
    sandbox.write("games/alpha/game.json", R"({
        "schema_version": 1, "name": "alpha", "modules": ["nope"],
        "prefabs": [], "scenes": [], "start_scene": "main"
    })");
    sandbox.write("games/not_a_game/readme.txt", "no manifest here");

    const Run text = gectl({"list", "--modules", modules, "--games", games});
    CHECK(text.code == 0);
    CHECK(has(text.out, "alpha  \"Alpha\"  0.1.0  800x600  1 modules  INVALID"));
    CHECK(has(text.out, "unknown module"));
    CHECK(has(text.out, "beta  \"Beta Game\"  2.0.1  480x720  1 modules  valid"));
    CHECK(!has(text.out, "not_a_game"));
    CHECK(text.out.find("alpha") < text.out.find("beta")); // sorted by directory

    const Run json = gectl({"list", "--modules", modules, "--games", games, "--json"});
    CHECK(json.code == 0);
    const auto parsed = ge::json::parse(json.out);
    REQUIRE(parsed.ok());
    const auto* list = parsed.value.find("games");
    REQUIRE(list != nullptr);
    REQUIRE(list->items().size() == 2U);
    CHECK(list->items()[0].find("valid")->as_bool() == false);
    CHECK(list->items()[1].find("title")->as_string() == "Beta Game");
    CHECK(list->items()[1].find("directory")->as_string() == games + "/beta");

    CHECK(gectl({"list", "--modules", modules, "--games", sandbox.dir() + "/missing"}).code == 1);
    CHECK(gectl({"list", "--modules", modules, "--games", games, "extra"}).code == 2);
    CHECK(gectl({"list", "--modules", modules, "--games"}).code == 2);
}

TEST_CASE("gectl: every game of the repository is valid") {
    const std::string root = std::string(GE_SOURCE_DIR);
    int games = 0;
    for (const auto& entry : fs::directory_iterator(root + "/games")) {
        const fs::path manifest = entry.path() / "game.json";
        if (!fs::exists(manifest)) {
            continue;
        }
        ++games;
        const Run run = gectl({"validate", "--modules", root + "/modules", manifest.string()});
        CHECK(run.err.empty());
        CHECK(run.code == 0);
    }
    CHECK(games >= 1);
}

TEST_CASE("gectl: schema writes the editor schemas, and notices when they are stale") {
    const Sandbox sandbox("schema");
    sandbox.write("modules/a/module.json", module_json("a"));
    const std::string modules = sandbox.dir() + "/modules";
    const std::string out_dir = sandbox.dir() + "/out";
    CHECK(gectl({"schema", "--modules", modules, "--out", out_dir, "--check"}).code == 1);
    const Run written = gectl({"schema", "--modules", modules, "--out", out_dir});
    CHECK(written.code == 0);
    CHECK(has(written.out, "game.schema.json"));
    CHECK(has(sandbox.read("out/prefab.schema.json"), "\"a.C\""));
    CHECK(gectl({"schema", "--modules", modules, "--out", out_dir, "--check"}).code == 0);
    sandbox.write("out/scene.schema.json", "{}");
    CHECK(gectl({"schema", "--modules", modules, "--out", out_dir, "--check"}).code == 1);
    CHECK(gectl({"schema", "--out"}).code == 2);
    CHECK(gectl({"schema", "--modules", "/definitely/not/here"}).code == 1);
}

TEST_CASE("gectl: the schemas of the repository are up to date") {
    const std::string root = std::string(GE_SOURCE_DIR);
    const Run run = gectl(
        {"schema", "--check", "--modules", root + "/modules", "--out", root + "/schemas/game"});
    CHECK(run.err.empty());
    CHECK(run.code == 0);
}

TEST_CASE("gectl: new-module creates a module that is valid and generated") {
    const Sandbox sandbox("new_module");
    sandbox.write("a/module.json", module_json("a"));
    const std::string modules = sandbox.dir();
    const Run created = gectl({"new-module", "b", "--depends", "a", "--modules", modules});
    CHECK(created.code == 0);
    CHECK(has(created.out, "wrote "));
    CHECK(has(created.out, "next: edit"));
    const std::string manifest = sandbox.read("b/module.json");
    CHECK(has(manifest, "\"name\": \"b\""));
    CHECK(has(manifest, "\"depends\": [\"a\"]"));
    CHECK(has(sandbox.read("b/src/b.cpp"), "void step(ecs::World& world)"));
    const std::string test = sandbox.read("b/tests/test_b.cpp");
    CHECK(has(test, "ge::mod::a::install(app);"));
    CHECK(test.find("ge/mod/a/a.gen.hpp") < test.find("ge/mod/b/b.gen.hpp"));
    CHECK(has(sandbox.read("b/gen/b.gen.cpp"), "namespace ge::mod::b"));
    // The new module is part of the set: it checks and its generated code is current.
    CHECK(gectl({"check", "--modules", modules}).code == 0);
    CHECK(gectl({"generate", "--check", "b", "--modules", modules}).code == 0);
}

TEST_CASE("gectl: new-module refuses what would make the modules invalid") {
    const Sandbox sandbox("new_module_refused");
    sandbox.write("a/module.json", module_json("a"));
    const std::string modules = sandbox.dir();
    const Run exists = gectl({"new-module", "a", "--modules", modules});
    CHECK(exists.code == 1);
    CHECK(has(exists.err, "the module already exists"));
    const Run unknown = gectl({"new-module", "c", "--depends", "zzz", "--modules", modules});
    CHECK(unknown.code == 1);
    CHECK(has(unknown.err, "zzz"));
    CHECK(sandbox.read("c/module.json").empty()); // nothing was written
    const Run bad_name = gectl({"new-module", "Bad-Name", "--modules", modules});
    CHECK(bad_name.code == 1);
    CHECK(sandbox.read("Bad-Name/module.json").empty());
    const Run reserved = gectl({"new-module", "core", "--modules", modules});
    CHECK(reserved.code == 1);
    CHECK(gectl({"new-module", "--modules", modules}).code == 2);
    CHECK(gectl({"new-module", "x", "y", "--modules", modules}).code == 2);
    CHECK(gectl({"new-module", "x", "--depends"}).code == 2);
    CHECK(gectl({"new-module", "x", "--modules", "/definitely/not/here"}).code == 1);
}
