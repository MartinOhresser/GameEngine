#include "ge/app/app.hpp"

#include "ge/core/assert.hpp"
#include "ge/core/random.hpp"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <optional>

namespace ge::app {

namespace {

constexpr std::size_t index_of(Stage stage) noexcept {
    return static_cast<std::size_t>(stage);
}

constexpr std::uint64_t nanoseconds_per_second = 1'000'000'000;

std::string concat(std::initializer_list<std::string_view> parts) {
    std::string result;
    for (const std::string_view part : parts) {
        result.append(part);
    }
    return result;
}

/// "Run before" relations between the systems of one stage, by index.
struct Graph {
    std::vector<std::vector<std::size_t>> successors;
    std::vector<std::size_t> waiting_on;
};

template <typename Entries>
std::optional<std::size_t> find_system(const Entries& systems, const std::string& name) {
    for (std::size_t i = 0; i < systems.size(); ++i) {
        if (systems[i].name == name) {
            return i;
        }
    }
    return std::nullopt;
}

/// Adds the edges for one list of constraints of system `self`: each name in
/// `others` runs first when `others_first` is true, last otherwise.
template <typename Entries, typename Report>
void link_constraints(Graph& graph, const Entries& systems, std::size_t self,
                      const std::vector<std::string>& others, bool others_first,
                      std::string_view stage_name, const Report& report) {
    for (const std::string& other : others) {
        const auto found = find_system(systems, other);
        if (!found) {
            report(concat({"stage ", stage_name, ": '", systems[self].name,
                           others_first ? "' runs after '" : "' runs before '", other,
                           "', which does not exist"}));
            continue;
        }
        const std::size_t first = others_first ? *found : self;
        const std::size_t second = others_first ? self : *found;
        graph.successors[first].push_back(second);
        ++graph.waiting_on[second];
    }
}

template <typename Entries, typename Report>
Graph build_graph(const Entries& systems, std::string_view stage_name, const Report& report) {
    Graph graph{std::vector<std::vector<std::size_t>>(systems.size()),
                std::vector<std::size_t>(systems.size(), 0)};
    for (std::size_t i = 0; i < systems.size(); ++i) {
        link_constraints(graph, systems, i, systems[i].order.runs_after, true, stage_name, report);
        link_constraints(graph, systems, i, systems[i].order.runs_before, false, stage_name,
                         report);
    }
    return graph;
}

/// The earliest-registered system that is not placed and waits on nothing.
std::optional<std::size_t> first_ready(const Graph& graph, const std::vector<bool>& placed) {
    for (std::size_t i = 0; i < placed.size(); ++i) {
        if (!placed[i] && graph.waiting_on[i] == 0) {
            return i;
        }
    }
    return std::nullopt;
}

template <typename Entries, typename Report>
void report_cycle(const Entries& systems, const std::vector<bool>& placed,
                  std::string_view stage_name, const Report& report) {
    std::string members;
    for (std::size_t i = 0; i < placed.size(); ++i) {
        if (!placed[i]) {
            members += members.empty() ? "'" : ", '";
            members += systems[i].name;
            members += "'";
        }
    }
    report(concat({"stage ", stage_name, ": ordering constraints form a cycle among ", members}));
}

/// Takes the systems in dependency order. Systems caught in a cycle are
/// appended in registration order, so they still run.
template <typename Entries, typename Report>
std::vector<std::size_t> order_by_graph(Graph graph, const Entries& systems,
                                        std::string_view stage_name, const Report& report) {
    std::vector<std::size_t> order;
    order.reserve(systems.size());
    std::vector<bool> placed(systems.size(), false);
    while (order.size() < systems.size()) {
        const auto next = first_ready(graph, placed);
        if (!next) {
            report_cycle(systems, placed, stage_name, report);
            for (std::size_t i = 0; i < placed.size(); ++i) {
                if (!placed[i]) {
                    order.push_back(i);
                }
            }
            break;
        }
        placed[*next] = true;
        order.push_back(*next);
        for (const std::size_t successor : graph.successors[*next]) {
            --graph.waiting_on[successor];
        }
    }
    return order;
}

} // namespace

std::string_view to_string(Stage stage) noexcept {
    switch (stage) {
    case Stage::Startup:
        return "Startup";
    case Stage::PreUpdate:
        return "PreUpdate";
    case Stage::FixedUpdate:
        return "FixedUpdate";
    case Stage::Update:
        return "Update";
    case Stage::PostUpdate:
        return "PostUpdate";
    }
    return "Unknown"; // GCOVR_EXCL_LINE: unreachable for valid enum values
}

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index): `Stage` values are
// always smaller than `stage_count`.
void Schedule::add_system(Stage stage, System system, std::string name, Order order) {
    auto& systems = stages_[index_of(stage)];
    if (name.empty()) {
        name = "system_" + std::to_string(systems.size());
    }
    systems.push_back({std::move(name), std::move(system), std::move(order)});
    order_dirty_[index_of(stage)] = true;
}

void Schedule::run_stage(Stage stage, ecs::World& world) {
    const std::size_t slot = index_of(stage);
    if (order_dirty_[slot]) {
        order_cache_[slot] = execution_order(stage, nullptr);
        order_dirty_[slot] = false;
    }
    for (const std::size_t entry : order_cache_[slot]) {
        stages_[slot][entry].run(world);
        if (auto* commands = world.resource<ecs::Commands>(); commands != nullptr) {
            commands->apply(world);
        }
    }
}

std::size_t Schedule::system_count(Stage stage) const noexcept {
    return stages_[index_of(stage)].size();
}

std::vector<std::string> Schedule::system_names(Stage stage) const {
    std::vector<std::string> names;
    for (const std::size_t entry : execution_order(stage, nullptr)) {
        names.push_back(stages_[index_of(stage)][entry].name);
    }
    return names;
}

std::vector<std::string> Schedule::problems() const {
    std::vector<std::string> found;
    for (std::size_t slot = 0; slot < stage_count; ++slot) {
        const auto stage = static_cast<Stage>(slot);
        const auto& systems = stages_[slot];
        for (std::size_t i = 0; i < systems.size(); ++i) {
            for (std::size_t j = i + 1; j < systems.size(); ++j) {
                if (systems[i].name == systems[j].name) {
                    found.push_back(concat({"stage ", to_string(stage), ": two systems are named '",
                                            systems[i].name, "'"}));
                }
            }
        }
        static_cast<void>(execution_order(stage, &found));
    }
    return found;
}

std::vector<std::size_t> Schedule::execution_order(Stage stage,
                                                   std::vector<std::string>* problems) const {
    const auto& systems = stages_[index_of(stage)];
    const auto report = [problems](std::string message) {
        if (problems != nullptr) {
            problems->push_back(std::move(message));
        }
    };
    const std::string_view stage_name = to_string(stage);
    return order_by_graph(build_graph(systems, stage_name, report), systems, stage_name, report);
}

// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)

App::App() {
    world_.insert_resource(Time{});
    world_.insert_resource(Random{});
    world_.insert_resource(ecs::Commands{});
    set_tick_rate(60);
}

void App::set_tick_rate(std::uint32_t ticks_per_second) {
    GE_ASSERT(ticks_per_second >= 1);
    ticks_per_second = std::max<std::uint32_t>(ticks_per_second, 1);
    step_ns_ = nanoseconds_per_second / ticks_per_second;
    Time* time = world_.resource<Time>();
    GE_ASSERT(time != nullptr);
    if (time != nullptr) {
        time->tick_rate = ticks_per_second;
        time->fixed_delta = 1.0F / static_cast<float>(ticks_per_second);
    }
}

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)
void App::add_stage_hook(Stage stage, System hook) {
    hooks_[index_of(stage)].push_back(std::move(hook));
}

void App::run_stage(Stage stage) {
    schedule_.run_stage(stage, world_);
    for (const System& hook : hooks_[index_of(stage)]) {
        hook(world_);
    }
}
// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)

void App::run_startup_once() {
    if (!started_) {
        run_stage(Stage::Startup);
        started_ = true;
    }
}

void App::advance(std::chrono::nanoseconds real_delta) {
    run_startup_once();

    const std::uint64_t elapsed_ns =
        real_delta.count() > 0 ? static_cast<std::uint64_t>(real_delta.count()) : 0;
    accumulator_ns_ += elapsed_ns;
    if (Time* time = world_.resource<Time>(); time != nullptr) {
        time->frame_delta = static_cast<float>(elapsed_ns) * 1e-9F;
    }

    run_stage(Stage::PreUpdate);

    std::uint32_t ticks_run = 0;
    while (accumulator_ns_ >= step_ns_ && ticks_run < max_catch_up_ticks_) {
        run_stage(Stage::FixedUpdate);
        accumulator_ns_ -= step_ns_;
        ++ticks_run;
        if (Time* time = world_.resource<Time>(); time != nullptr) {
            ++time->tick;
        }
    }
    // Time the cap left over is dropped; only the fraction of a tick is kept.
    accumulator_ns_ %= step_ns_;
    if (Time* time = world_.resource<Time>(); time != nullptr) {
        time->alpha = static_cast<float>(accumulator_ns_) / static_cast<float>(step_ns_);
    }

    run_stage(Stage::Update);
    run_stage(Stage::PostUpdate);
    ++frames_;
}

void App::update() {
    advance(std::chrono::nanoseconds(static_cast<std::int64_t>(step_ns_)));
}

void App::run_frames(std::uint32_t frames) {
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        update();
    }
}

std::uint64_t App::tick_count() const noexcept {
    const Time* time = world_.resource<Time>();
    return time != nullptr ? time->tick : 0;
}

} // namespace ge::app
