#include "scaffold.hpp"

#include <algorithm>

namespace ge::tools {

namespace {

std::string replace_all(std::string text, const std::string& from, const std::string& to) {
    for (std::size_t at = text.find(from); at != std::string::npos;
         at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
    }
    return text;
}

/// "ge/mod/<name>/<name>.gen.hpp"
std::string header_of(const std::string& name) {
    std::string header = "ge/mod/";
    header += name;
    header += '/';
    header += name;
    header += ".gen.hpp";
    return header;
}

std::string depends_json(const std::vector<std::string>& depends) {
    std::string out = "[";
    for (std::size_t i = 0; i < depends.size(); ++i) {
        out += (i == 0 ? "\"" : ", \"") + depends[i] + "\"";
    }
    return out + "]";
}

constexpr const char* module_json_template = R"({
  "$schema": "../../schemas/module.schema.json",
  "schema_version": 1,
  "name": "@NAME@",
  "version": "0.1.0",
  "description": "TODO: one sentence on what this module adds to a game.",
  "depends": @DEPENDS@,
  "components": [
    {
      "name": "Example",
      "description": "TODO: replace with the module's real components.",
      "replicable": true,
      "fields": [{ "name": "value", "type": "i32", "default": 0 }]
    }
  ],
  "systems": [
    {
      "name": "step",
      "description": "TODO: replace with the module's real systems.",
      "stage": "FixedUpdate",
      "writes": ["Example"]
    }
  ]
}
)";

constexpr const char* source_template = R"(#include "ge/mod/@NAME@/@NAME@.gen.hpp"

namespace ge::mod::@NAME@ {

void step(ecs::World& world) {
    world.each<Example>([](ecs::Entity, Example& example) { ++example.value; });
}

} // namespace ge::mod::@NAME@
)";

constexpr const char* test_template = R"(@INCLUDES@
#include "mini_test.hpp"

TEST_CASE("@NAME@: the example system counts ticks") {
    ge::app::App app;
@INSTALLS@    ge::mod::@NAME@::install(app);
    CHECK(app.module_problems().empty());
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, ge::mod::@NAME@::Example{}));
    app.run_frames(3);
    CHECK(app.world().get<ge::mod::@NAME@::Example>(entity)->value == 3);
}
)";

} // namespace

std::vector<ScaffoldFile> scaffold_module(const std::string& name,
                                          const std::vector<std::string>& depends) {
    // Includes are kept sorted, the way clang-format wants them.
    std::vector<std::string> headers = {"ge/app/app.hpp", header_of(name)};
    std::string installs;
    for (const std::string& dependency : depends) {
        headers.push_back(header_of(dependency));
        installs += "    ge::mod::";
        installs += dependency;
        installs += "::install(app);\n";
    }
    std::sort(headers.begin(), headers.end());
    std::string includes;
    for (const std::string& header : headers) {
        includes += "#include \"" + header + "\"\n";
    }
    const auto fill = [&](const char* text) {
        std::string result = replace_all(text, "@NAME@", name);
        result = replace_all(result, "@DEPENDS@", depends_json(depends));
        result = replace_all(result, "@INCLUDES@", includes);
        return replace_all(result, "@INSTALLS@", installs);
    };
    return {
        {"module.json", fill(module_json_template)},
        {"src/" + name + ".cpp", fill(source_template)},
        {"tests/test_" + name + ".cpp", fill(test_template)},
    };
}

} // namespace ge::tools
