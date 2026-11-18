#include "gectl.hpp"

#include "ge/core/version.hpp"
#include "ge/game/game.hpp"
#include "ge/game/schema.hpp"
#include "ge/json/json.hpp"
#include "ge/module/generate.hpp"
#include "ge/module/spec.hpp"

#include "scaffold.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <ostream>
#include <sstream>
#include <system_error>

namespace ge::tools {

namespace fs = std::filesystem;

namespace {

constexpr int exit_ok = 0;
constexpr int exit_invalid = 1;
constexpr int exit_usage = 2;

constexpr const char* usage_text =
    "gectl - tools for the game engine's modules\n"
    "\n"
    "Usage:\n"
    "  gectl check    [--modules DIR]\n"
    "      Validate every DIR/*/module.json and the links between modules.\n"
    "  gectl catalog  [--modules DIR] [--compact]\n"
    "      Print what the engine and its modules offer, as JSON.\n"
    "  gectl generate [--modules DIR] [--check] [MODULE...]\n"
    "      Write the C++ generated from module.json into DIR/MODULE/gen.\n"
    "      With --check, write nothing and fail if the files are out of date.\n"
    "  gectl validate [--modules DIR] GAME.json\n"
    "      Check a game's manifest, prefabs and scenes against the modules.\n"
    "  gectl schema   [--modules DIR] [--out DIR] [--check]\n"
    "      Write JSON Schemas for game files (completion and checks in editors).\n"
    "  gectl new-module NAME [--depends A,B] [--modules DIR]\n"
    "      Create DIR/NAME with a working example (module.json, system, test).\n"
    "  gectl help\n"
    "\n"
    "DIR defaults to ./modules. Exit codes: 0 success, 1 invalid or stale, 2 usage.\n";

struct Options {
    std::string modules_dir = "modules";
    std::string out_dir = "schemas/game";
    bool compact = false;
    bool check_only = false;
    std::vector<std::string> names;
    std::vector<std::string> depends;
};

std::vector<std::string> split_commas(const std::string& text) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find(',', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        if (end > start) {
            parts.push_back(text.substr(start, end - start));
        }
        start = end + 1;
    }
    return parts;
}

/// Parses the arguments after the command. Returns an error message, or empty.
std::string parse_options(const std::vector<std::string>& args, Options& options) {
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--modules") {
            if (i + 1 >= args.size()) {
                return "--modules needs a directory";
            }
            options.modules_dir = args[++i];
        } else if (arg == "--out") {
            if (i + 1 >= args.size()) {
                return "--out needs a directory";
            }
            options.out_dir = args[++i];
        } else if (arg == "--depends") {
            if (i + 1 >= args.size()) {
                return "--depends needs a comma-separated list of modules";
            }
            options.depends = split_commas(args[++i]);
        } else if (arg == "--compact") {
            options.compact = true;
        } else if (arg == "--check") {
            options.check_only = true;
        } else if (arg.starts_with("--")) {
            return "unknown option " + arg;
        } else {
            options.names.push_back(arg);
        }
    }
    return {};
}

bool read_file(const fs::path& path, std::string& content) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }
    content.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    return true;
}

struct Loaded {
    std::vector<module::ModuleSpec> modules;
    std::vector<std::string> files;
    std::vector<fs::path> directories;
    std::vector<module::Diagnostic> problems;
};

/// Reads and validates every module under `dir`, then checks them together.
Loaded load_all(const std::string& dir) {
    Loaded loaded;
    std::error_code error;
    std::vector<fs::path> directories;
    for (fs::directory_iterator it(dir, error), end; !error && it != end; it.increment(error)) {
        if (it->is_directory(error)) {
            directories.push_back(it->path());
        }
    }
    if (error) {
        loaded.problems.push_back({dir, "", "cannot read the modules directory"});
        return loaded;
    }
    std::sort(directories.begin(), directories.end());

    for (const fs::path& directory : directories) {
        const fs::path file = directory / "module.json";
        const std::string label = file.generic_string();
        std::string text;
        if (!read_file(file, text)) {
            loaded.problems.push_back({label, "", "cannot read module.json"});
            continue;
        }
        module::LoadResult result = module::load_module(text, label);
        const bool name_ok =
            result.spec.name.empty() || result.spec.name == directory.filename().generic_string();
        if (!name_ok) {
            result.diagnostics.push_back({label, "name",
                                          "the module is named \"" + result.spec.name +
                                              "\" but lives in the directory \"" +
                                              directory.filename().generic_string() + "\""});
        }
        for (module::Diagnostic& problem : result.diagnostics) {
            loaded.problems.push_back(std::move(problem));
        }
        loaded.modules.push_back(std::move(result.spec));
        loaded.files.push_back(label);
        loaded.directories.push_back(directory);
    }

    if (loaded.problems.empty()) {
        loaded.problems = module::check_modules(loaded.modules, loaded.files);
    }
    return loaded;
}

int report(const Loaded& loaded, std::ostream& err) {
    for (const module::Diagnostic& problem : loaded.problems) {
        err << problem.to_string() << '\n';
    }
    err << loaded.problems.size() << (loaded.problems.size() == 1 ? " problem" : " problems")
        << " found\n";
    return exit_invalid;
}

json::Value engine_description() {
    const auto names = [](const std::vector<std::string_view>& items) {
        json::Value array = json::Value::array();
        for (const std::string_view item : items) {
            array.push_back(json::Value::string(std::string(item)));
        }
        return array;
    };
    const auto resource = [](const char* name, const char* description) {
        json::Value entry = json::Value::object();
        entry.set("name", json::Value::string(name));
        entry.set("description", json::Value::string(description));
        return entry;
    };
    json::Value resources = json::Value::array();
    resources.push_back(
        resource("Time", "Fixed tick counter and clock; gameplay reads tick and fixed_delta."));
    resources.push_back(resource("Random", "Seeded deterministic generator; the only allowed "
                                           "source of randomness."));
    resources.push_back(resource("Commands", "Deferred world changes, applied after each system."));
    resources.push_back(
        resource("Events<T>", "Per-type event queue, created with App::add_event<T>()."));
    resources.push_back(resource("Actions", "What the player is doing, as named values (move_x, "
                                            "jump); game logic reads actions, never a keyboard."));
    resources.push_back(resource("ScriptedInput", "Action changes at given ticks: the player of "
                                                  "tests and scenarios."));

    json::Value engine = json::Value::object();
    engine.set("version", json::Value::string(std::string(ge::version())));
    engine.set("stages", names(module::stage_names()));
    engine.set("field_types", names(module::field_type_names()));
    engine.set("resources", std::move(resources));
    return engine;
}

int command_check(const Options& options, std::ostream& out, std::ostream& err) {
    const Loaded loaded = load_all(options.modules_dir);
    if (!loaded.problems.empty()) {
        return report(loaded, err);
    }
    out << "ok: " << loaded.modules.size() << (loaded.modules.size() == 1 ? " module" : " modules")
        << " in " << options.modules_dir << '\n';
    return exit_ok;
}

int command_catalog(const Options& options, std::ostream& out, std::ostream& err) {
    const Loaded loaded = load_all(options.modules_dir);
    if (!loaded.problems.empty()) {
        return report(loaded, err);
    }
    json::Value modules = json::Value::array();
    for (const module::ModuleSpec& spec : loaded.modules) {
        modules.push_back(module::catalog_entry(spec));
    }
    json::Value catalog = json::Value::object();
    catalog.set("schema_version", json::Value::integer(1));
    catalog.set("engine", engine_description());
    catalog.set("modules", std::move(modules));
    out << json::dump(catalog, options.compact ? 0 : 2) << '\n';
    return exit_ok;
}

/// Brings one generated file up to date, or only reports that it is not
/// (`check_only`). Returns false when the file is stale or cannot be written.
bool sync_file(const fs::path& target, const std::string& content, bool check_only,
               std::ostream& out, std::ostream& err) {
    std::string current;
    if (read_file(target, current) && current == content) {
        return true;
    }
    if (check_only) {
        err << target.generic_string() << ": out of date, run `gectl generate` or `gectl schema`\n";
        return false;
    }
    std::error_code error;
    fs::create_directories(target.parent_path(), error);
    std::ofstream stream(target, std::ios::binary | std::ios::trunc);
    stream << content;
    if (error || !stream) {
        err << target.generic_string() << ": cannot write the file\n";
        return false;
    }
    out << "wrote " << target.generic_string() << '\n';
    return true;
}

int command_generate(const Options& options, std::ostream& out, std::ostream& err) {
    const Loaded loaded = load_all(options.modules_dir);
    if (!loaded.problems.empty()) {
        return report(loaded, err);
    }
    for (const std::string& name : options.names) {
        const auto named = [&name](const module::ModuleSpec& m) { return m.name == name; };
        if (std::none_of(loaded.modules.begin(), loaded.modules.end(), named)) {
            err << "no module named \"" << name << "\" in " << options.modules_dir << '\n';
            return exit_usage;
        }
    }

    bool all_synced = true;
    for (std::size_t i = 0; i < loaded.modules.size(); ++i) {
        const module::ModuleSpec& spec = loaded.modules[i];
        const bool selected =
            options.names.empty() ||
            std::find(options.names.begin(), options.names.end(), spec.name) != options.names.end();
        if (!selected) {
            continue;
        }
        for (const module::GeneratedFile& file : module::generate(spec)) {
            const fs::path target = loaded.directories[i] / "gen" / file.path;
            all_synced =
                sync_file(target, file.content, options.check_only, out, err) && all_synced;
        }
    }
    return all_synced ? exit_ok : exit_invalid;
}

int command_schema(const Options& options, std::ostream& out, std::ostream& err) {
    const Loaded loaded = load_all(options.modules_dir);
    if (!loaded.problems.empty()) {
        return report(loaded, err);
    }
    bool all_synced = true;
    for (const game::SchemaFile& file :
         game::editor_schemas(game::Catalog::from_specs(loaded.modules))) {
        const std::string content = json::dump(file.schema) + "\n";
        all_synced = sync_file(fs::path(options.out_dir) / file.path, content, options.check_only,
                               out, err) &&
                     all_synced;
    }
    return all_synced ? exit_ok : exit_invalid;
}

/// Writes a file, creating its directories. Returns false and says why on failure.
bool write_new_file(const fs::path& target, const std::string& content, std::ostream& err) {
    std::error_code error;
    fs::create_directories(target.parent_path(), error);
    std::ofstream stream(target, std::ios::binary | std::ios::trunc);
    stream << content;
    if (error || !stream) {
        err << target.generic_string() << ": cannot write the file\n";
        return false;
    }
    return true;
}

int command_new_module(const Options& options, std::ostream& out, std::ostream& err) {
    if (options.names.size() != 1) {
        err << "new-module needs exactly one module name\n\n" << usage_text;
        return exit_usage;
    }
    const std::string& name = options.names.front();
    const fs::path directory = fs::path(options.modules_dir) / name;
    Loaded loaded = load_all(options.modules_dir);
    if (!loaded.problems.empty()) {
        return report(loaded, err);
    }
    if (fs::exists(directory)) {
        err << directory.generic_string() << ": the module already exists\n";
        return exit_invalid;
    }

    const std::vector<ScaffoldFile> files = scaffold_module(name, options.depends);
    const std::string label = (directory / "module.json").generic_string();
    module::LoadResult created = module::load_module(files.front().content, label);
    loaded.modules.push_back(created.spec);
    loaded.files.push_back(label);
    std::vector<module::Diagnostic> problems = std::move(created.diagnostics);
    if (problems.empty()) {
        problems = module::check_modules(loaded.modules, loaded.files);
    }
    for (const module::Diagnostic& problem : problems) {
        err << problem.to_string() << '\n';
    }
    if (!problems.empty()) {
        return exit_invalid;
    }

    for (const ScaffoldFile& file : files) {
        if (!write_new_file(directory / file.path, file.content, err)) {
            return exit_invalid;
        }
        out << "wrote " << (directory / file.path).generic_string() << '\n';
    }
    for (const module::GeneratedFile& file : module::generate(created.spec)) {
        if (!write_new_file(directory / "gen" / file.path, file.content, err)) {
            return exit_invalid;
        }
        out << "wrote " << (directory / "gen" / file.path).generic_string() << '\n';
    }
    out << "next: edit " << label << " and src/" << name << ".cpp, run `gectl generate " << name
        << "`, then re-run CMake; the module is picked up from its directory\n";
    return exit_ok;
}

int command_validate(const Options& options, std::ostream& out, std::ostream& err) {
    if (options.names.size() != 1) {
        err << "validate needs exactly one game file\n\n" << usage_text;
        return exit_usage;
    }
    const Loaded loaded = load_all(options.modules_dir);
    if (!loaded.problems.empty()) {
        return report(loaded, err);
    }
    const game::FileReader read = [](const std::string& path) -> std::optional<std::string> {
        std::string text;
        if (!read_file(path, text)) {
            return std::nullopt;
        }
        return text;
    };
    const game::LoadedGame game =
        game::load_game(options.names.front(), read, game::Catalog::from_specs(loaded.modules));
    for (const module::Diagnostic& problem : game.diagnostics) {
        err << problem.to_string() << '\n';
    }
    if (!game.ok()) {
        return exit_invalid;
    }
    out << options.names.front() << ": valid (" << game.game.prefabs.size() << " prefabs, "
        << game.game.scenes.size() << " scenes)\n";
    return exit_ok;
}

} // namespace

int run_gectl(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    if (args.empty()) {
        err << usage_text;
        return exit_usage;
    }
    const std::string& command = args.front();
    if (command == "help" || command == "--help" || command == "-h") {
        out << usage_text;
        return exit_ok;
    }
    if (command != "check" && command != "catalog" && command != "generate" &&
        command != "validate" && command != "schema" && command != "new-module") {
        err << "unknown command \"" << command << "\"\n\n" << usage_text;
        return exit_usage;
    }
    Options options;
    if (const std::string problem = parse_options(args, options); !problem.empty()) {
        err << problem << "\n\n" << usage_text;
        return exit_usage;
    }
    if (command == "check") {
        return command_check(options, out, err);
    }
    if (command == "catalog") {
        return command_catalog(options, out, err);
    }
    if (command == "new-module") {
        return command_new_module(options, out, err);
    }
    if (command == "schema") {
        return command_schema(options, out, err);
    }
    if (command == "validate") {
        return command_validate(options, out, err);
    }
    return command_generate(options, out, err);
}

} // namespace ge::tools
