#include "ge/app/host.hpp"
#include "ge/app/input.hpp"
#include "ge/app/lists.hpp"
#include "ge/app/runtime_lists.hpp"
#include "ge/app/viewport.hpp"
#include "ge/game/scenario.hpp"
#include "ge/platform/backend.hpp"

#include <algorithm>
#include <cmath>

namespace ge::game {

namespace {

/// Numbers that differ by less than this (relative to their size) are equal:
/// game values are floats, so "== 0.1" must not depend on the last bit.
constexpr double tolerance = 1e-4;

bool holds(Compare compare, double actual, double expected) {
    const double scale = std::max(1.0, std::abs(expected));
    const bool equal = std::abs(actual - expected) <= tolerance * scale;
    switch (compare) {
    case Compare::Eq:
        return equal;
    case Compare::Ne:
        return !equal;
    case Compare::Lt:
        return actual < expected && !equal;
    case Compare::Le:
        return actual < expected || equal;
    case Compare::Gt:
        return actual > expected && !equal;
    case Compare::Ge:
        return actual > expected || equal;
    }
    return false; // GCOVR_EXCL_LINE: unreachable for valid enum values
}

double as_number(const json::Value& value) {
    if (value.is_bool()) {
        return value.as_bool() ? 1.0 : 0.0;
    }
    return value.as_real();
}

double scalar_number(const app::Scalar& scalar) {
    return app::scalar_as<double>(scalar);
}

/// 20 rather than 20.0 for whole numbers.
std::string show(double number) {
    if (std::abs(number) < 1e15 && number == std::floor(number)) {
        return std::to_string(static_cast<long long>(number));
    }
    return json::dump(json::Value::real(number), 0);
}

/// One scenario run: the app, and the entities named by the start scene.
struct Run {
    app::App app;
    SetupResult setup;
    const Game* game = nullptr; ///< The game being played, for the names of its scenes.
};

/// The entity a scenario names: looked up in the running world, so that a
/// scene change is followed, with the start scene's names as a fallback.
ecs::Entity find_named(const Run& run, std::string_view name) {
    if (const auto* names = run.app.world().resource<app::EntityNames>(); names != nullptr) {
        const ecs::Entity found = names->find(name);
        if (run.app.world().is_alive(found)) {
            return found;
        }
    }
    return run.setup.find(name);
}

std::string failure(const Check& check, const std::string& detail) {
    return "tick " + std::to_string(check.at) + ": \"" + check.text + "\" is false (" + detail +
           ")";
}

void check_field(const Run& run, const Check& check, std::vector<std::string>& failures) {
    const ecs::Entity entity = find_named(run, check.entity);
    if (!run.app.world().is_alive(entity)) {
        failures.push_back(failure(check, "\"" + check.entity + "\" no longer exists"));
        return;
    }
    const app::ComponentInfo* component = find_component_info(run.app, check.component);
    const std::optional<app::Scalar> actual =
        component != nullptr && component->read != nullptr
            ? component->read(run.app.world(), entity, check.field)
            : std::nullopt;
    if (!actual) {
        failures.push_back(failure(check, "\"" + check.entity + "\" has no " + check.component));
        return;
    }
    const double number = scalar_number(*actual);
    if (!holds(check.compare, number, as_number(check.value))) {
        failures.push_back(failure(check, "the value is " + show(number)));
    }
}

/// A host command is "called" when it is waiting in the queue: no host drains it in a scenario.
void check_host_call(const Run& run, const Check& check, std::vector<std::string>& failures) {
    const std::vector<std::string> commands = run.game->ref_names("host_command");
    const auto found = std::find(commands.begin(), commands.end(), check.entity);
    const auto* requests = run.app.world().resource<app::HostRequests>();
    const auto index = static_cast<std::uint32_t>(found - commands.begin());
    const bool called =
        found != commands.end() && requests != nullptr &&
        std::any_of(requests->pending.begin(), requests->pending.end(),
                    [index](const app::HostRequest& request) { return request.command == index; });
    if (!called) {
        failures.push_back(failure(check, "the game did not call it"));
    }
}

void evaluate(const Run& run, const Check& check, std::vector<std::string>& failures) {
    switch (check.kind) {
    case CheckKind::Field:
        check_field(run, check, failures);
        break;
    case CheckKind::EntityCount: {
        const auto count = static_cast<double>(run.app.world().entity_count());
        if (!holds(check.compare, count, as_number(check.value))) {
            failures.push_back(failure(check, "there are " + show(count) + " entities"));
        }
        break;
    }
    case CheckKind::Alive:
    case CheckKind::Gone: {
        const bool alive = run.app.world().is_alive(find_named(run, check.entity));
        if (alive != (check.kind == CheckKind::Alive)) {
            failures.push_back(failure(
                check, "\"" + check.entity + (alive ? "\" still exists" : "\" no longer exists")));
        }
        break;
    }
    case CheckKind::Scene: {
        const auto* current = run.app.world().resource<app::CurrentScene>();
        const std::string actual = current != nullptr && current->index < run.game->scenes.size()
                                       ? run.game->scenes[current->index].name
                                       : std::string("(none)");
        if (actual != check.entity) {
            failures.push_back(failure(check, "the game is in the scene \"" + actual + "\""));
        }
        break;
    }
    case CheckKind::Quit: {
        const auto* quit = run.app.world().resource<app::QuitRequest>();
        if (quit == nullptr || !quit->requested) {
            failures.push_back(failure(check, "the game did not ask to quit"));
        }
        break;
    }
    case CheckKind::HostCall:
        check_host_call(run, check, failures);
        break;
    case CheckKind::ListSelected: {
        const auto* lists = run.app.world().resource<app::RuntimeLists>();
        const auto actual = static_cast<double>(lists == nullptr ? app::RuntimeLists::none
                                                                 : lists->selected(check.entity));
        if (!holds(check.compare, actual, as_number(check.value))) {
            failures.push_back(failure(check, "the selected item is " + show(actual)));
        }
        break;
    }
    case CheckKind::Fingerprint: {
        const std::uint64_t actual = run.app.world().fingerprint();
        if ((actual == check.fingerprint) != (check.compare == Compare::Eq)) {
            failures.push_back(failure(check, "the fingerprint is " + format_fingerprint(actual)));
        }
        break;
    }
    }
}

/// Runs the scenario once. Checks are only evaluated when `failures` is given.
/// Returns the fingerprint at the end, or nothing if the game did not start.
std::optional<std::uint64_t> run_once(const Game& game, const ScenarioData& scenario,
                                      std::span<const ModuleEntry> modules,
                                      std::vector<std::string>* failures,
                                      std::vector<std::string>& setup_problems) {
    Run run;
    Game started = game;
    if (!scenario.scene.empty()) {
        started.manifest.start_scene = scenario.scene;
    }
    run.game = &started;
    run.setup = setup_game(run.app, started, modules);
    if (!run.setup.ok()) {
        setup_problems = run.setup.problems;
        return std::nullopt;
    }
    if (scenario.screen_width > 0 && started.manifest.window_adapt) {
        // The drawing area takes the shape of the screen, as the window of a player does.
        auto* view = run.app.world().resource<app::Viewport>();
        const Manifest& manifest = started.manifest;
        const auto [width, height] = platform::adapted_view(
            static_cast<int>(manifest.window_width), static_cast<int>(manifest.window_height),
            manifest.window_min_aspect, manifest.window_max_aspect, scenario.screen_width,
            scenario.screen_height);
        view->width = static_cast<std::uint32_t>(width);
        view->height = static_cast<std::uint32_t>(height);
    }
    if (auto* lists = run.app.world().resource<app::RuntimeLists>(); lists != nullptr) {
        for (const auto& list : scenario.lists) {
            lists->set(list.first, list.second);
        }
    }
    auto* script = run.app.world().resource<app::ScriptedInput>();
    for (const ScenarioInput& input : scenario.inputs) {
        script->add(input.tick, input.action, input.value);
    }
    std::vector<const Check*> ordered;
    ordered.reserve(scenario.checks.size());
    for (const Check& check : scenario.checks) {
        ordered.push_back(&check);
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const Check* a, const Check* b) { return a->at < b->at; });

    std::uint64_t done = 0;
    const auto run_until = [&run, &done](std::uint64_t tick) {
        run.app.run_frames(static_cast<std::uint32_t>(tick - done));
        done = tick;
    };
    for (const Check* check : ordered) {
        run_until(check->at);
        if (failures != nullptr) {
            evaluate(run, *check, *failures);
        }
    }
    run_until(scenario.ticks);
    return run.app.world().fingerprint();
}

} // namespace

ScenarioReport run_scenario(const Game& game, const ScenarioData& scenario,
                            std::span<const ModuleEntry> modules) {
    ScenarioReport report;
    report.name = scenario.name;
    std::vector<std::string> setup_problems;
    const std::optional<std::uint64_t> first =
        run_once(game, scenario, modules, &report.failures, setup_problems);
    if (!first) {
        report.failures = std::move(setup_problems);
        return report;
    }
    report.fingerprint = *first;
    std::vector<std::string> ignored;
    const std::optional<std::uint64_t> second = run_once(game, scenario, modules, nullptr, ignored);
    if (second != first) {
        report.failures.push_back("not deterministic: two runs of the scenario ended in "
                                  "different states (" +
                                  format_fingerprint(*first) + " and " +
                                  format_fingerprint(second.value_or(0)) + ")");
    }
    return report;
}

} // namespace ge::game
