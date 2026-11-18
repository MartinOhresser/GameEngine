#include "runner.hpp"

#include "ge/game/scenario.hpp"

#include "registry.hpp"

#include <charconv>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <ostream>

namespace ge::runner {

namespace {

constexpr int exit_ok = 0;
constexpr int exit_invalid = 1;
constexpr int exit_usage = 2;

constexpr const char* usage_text =
    "gerun - run a game described by JSON files, without a window\n"
    "\n"
    "Usage: gerun GAME.json [--ticks N]\n"
    "       gerun GAME.json --test [--scenario NAME]\n"
    "\n"
    "Without --test, runs N ticks (default 60) and prints the number of\n"
    "entities and a fingerprint of the final state. With --test, runs the\n"
    "game's scenarios (scripted inputs and expectations) and reports each.\n"
    "Exit codes: 0 success, 1 invalid game or failed scenario, 2 usage.\n";

struct Arguments {
    std::string game_path;
    std::uint32_t ticks = 60;
    bool test = false;
    std::string scenario; ///< With --test: only this one.
};

/// Returns an error message, or empty.
std::string parse(const std::vector<std::string>& args, Arguments& out) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--ticks") {
            if (i + 1 >= args.size()) {
                return "--ticks needs a number";
            }
            const std::string& text = args[++i];
            const auto [end, error] =
                std::from_chars(text.data(), text.data() + text.size(), out.ticks);
            if (error != std::errc{} || end != text.data() + text.size()) {
                return "--ticks needs a whole number, not \"" + text + "\"";
            }
        } else if (arg == "--test") {
            out.test = true;
        } else if (arg == "--scenario") {
            if (i + 1 >= args.size()) {
                return "--scenario needs a name";
            }
            out.scenario = args[++i];
            out.test = true;
        } else if (arg.starts_with("--")) {
            return "unknown option " + arg;
        } else if (out.game_path.empty()) {
            out.game_path = arg;
        } else {
            return "only one game file can be given";
        }
    }
    return out.game_path.empty() ? "no game file given" : "";
}

std::optional<std::string> read_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

/// Runs the scenarios asked for and prints one line each, then the problems of those that failed.
int run_tests(const game::Game& game, const Arguments& arguments,
              std::span<const game::ModuleEntry> modules, std::ostream& out, std::ostream& err) {
    std::vector<const game::ScenarioData*> selected;
    for (const game::ScenarioData& scenario : game.scenarios) {
        if (arguments.scenario.empty() || scenario.name == arguments.scenario) {
            selected.push_back(&scenario);
        }
    }
    if (selected.empty()) {
        if (!arguments.scenario.empty()) {
            err << arguments.game_path << ": no scenario named \"" << arguments.scenario << "\"\n";
            return exit_invalid;
        }
        out << game.manifest.name << ": no scenarios\n";
        return exit_ok;
    }
    std::size_t failed = 0;
    for (const game::ScenarioData* scenario : selected) {
        const game::ScenarioReport report = game::run_scenario(game, *scenario, modules);
        out << (report.passed() ? "PASS " : "FAIL ") << game.manifest.name << '/' << report.name
            << " (" << scenario->ticks << " ticks)\n";
        for (const std::string& failure : report.failures) {
            out << "     " << failure << '\n';
        }
        failed += report.passed() ? 0U : 1U;
    }
    out << selected.size() - failed << " passed, " << failed << " failed\n";
    return failed == 0 ? exit_ok : exit_invalid;
}

} // namespace

int run_gerun(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    return run_gerun(args, linked_modules(), out, err);
}

int run_gerun(const std::vector<std::string>& args, std::span<const game::ModuleEntry> modules,
              std::ostream& out, std::ostream& err) {
    Arguments arguments;
    if (const std::string problem = parse(args, arguments); !problem.empty()) {
        err << problem << "\n\n" << usage_text;
        return exit_usage;
    }

    const game::LoadedGame loaded =
        game::load_game(arguments.game_path, read_file, game::catalog_of(modules));
    for (const module::Diagnostic& diagnostic : loaded.diagnostics) {
        err << diagnostic.to_string() << '\n';
    }
    if (!loaded.ok()) {
        return exit_invalid;
    }

    if (arguments.test) {
        return run_tests(loaded.game, arguments, modules, out, err);
    }

    app::App app;
    const game::SetupResult setup = game::setup_game(app, loaded.game, modules);
    for (const std::string& problem : setup.problems) {
        err << arguments.game_path << ": " << problem << '\n';
    }
    if (!setup.ok()) {
        return exit_invalid;
    }

    app.run_frames(arguments.ticks);
    out << loaded.game.manifest.name << ": " << arguments.ticks << " ticks, "
        << app.world().entity_count() << " entities, fingerprint "
        << game::format_fingerprint(app.world().fingerprint()) << '\n';
    return exit_ok;
}

} // namespace ge::runner
