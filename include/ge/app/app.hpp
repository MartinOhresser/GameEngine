#pragma once

#include "ge/app/module.hpp"
#include "ge/app/stage.hpp"
#include "ge/app/time.hpp"
#include "ge/ecs/commands.hpp"
#include "ge/ecs/events.hpp"
#include "ge/ecs/world.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace ge::app {

/// Any callable taking the world is a system.
using System = std::function<void(ecs::World&)>;

/// Ordering constraints between systems of the same stage, by name.
///
///     app.add_system(Stage::Update, movement, "movement", Order::after({"input"}));
struct Order {
    std::vector<std::string> runs_after;  ///< Run only after these systems.
    std::vector<std::string> runs_before; ///< Run before these systems.

    [[nodiscard]] static Order after(std::vector<std::string> names) {
        Order order;
        order.runs_after = std::move(names);
        return order;
    }

    [[nodiscard]] static Order before(std::vector<std::string> names) {
        Order order;
        order.runs_before = std::move(names);
        return order;
    }

    [[nodiscard]] static Order between(std::vector<std::string> after_names,
                                       std::vector<std::string> before_names) {
        Order order;
        order.runs_after = std::move(after_names);
        order.runs_before = std::move(before_names);
        return order;
    }
};

/// Systems grouped by stage. Within a stage they run in registration order,
/// except that a system with `Order` constraints waits until the systems it
/// must follow have run. Whenever several systems are ready, the one registered
/// first goes first, so the order is always the same for the same registrations.
class Schedule {
public:
    /// Adds a system. `name` shows up in `system_names` and is what other
    /// systems refer to in their `Order`; it defaults to `system_<n>`, where n
    /// is the system's position in the stage.
    void add_system(Stage stage, System system, std::string name = {}, Order order = {});

    /// Runs every system of one stage. When the world holds a `Commands`
    /// resource, its queue is applied after each system.
    void run_stage(Stage stage, ecs::World& world);

    [[nodiscard]] std::size_t system_count(Stage stage) const noexcept;

    /// Names of the systems of a stage, in execution order.
    [[nodiscard]] std::vector<std::string> system_names(Stage stage) const;

    /// Problems that make the schedule ambiguous or impossible: duplicate
    /// names, constraints naming an unknown system, cycles. Empty when fine.
    [[nodiscard]] std::vector<std::string> problems() const;

private:
    struct Entry {
        std::string name;
        System run;
        Order order;
    };

    /// Indices into the stage's entries, in execution order. Problems found on
    /// the way are appended to `problems` when it is not null.
    [[nodiscard]] std::vector<std::size_t>
    execution_order(Stage stage, std::vector<std::string>* problems) const;

    std::array<std::vector<Entry>, stage_count> stages_;
    std::array<std::vector<std::size_t>, stage_count> order_cache_;
    std::array<bool, stage_count> order_dirty_{};
};

class App;

/// A plugin is an object with `void build(App&)`, or any callable taking `App&`.
template <typename P>
concept PluginLike = requires(P& plugin, App& app) { plugin.build(app); } ||
                     requires(P& plugin, App& app) { plugin(app); };

/// A world, its schedule and the frame loop.
///
/// The simulation advances in fixed ticks (`Stage::FixedUpdate`), however fast
/// frames come: `advance` converts real elapsed time into whole ticks, and
/// `update` runs exactly one tick per frame, which is what tests and headless
/// runs want. `Time` and `Random` are available as resources from the start.
class App {
public:
    App();

    [[nodiscard]] ecs::World& world() noexcept { return world_; }
    [[nodiscard]] const ecs::World& world() const noexcept { return world_; }
    [[nodiscard]] const Schedule& schedule() const noexcept { return schedule_; }

    /// Number of frames run so far.
    [[nodiscard]] std::uint64_t frame_count() const noexcept { return frames_; }

    /// Applies a plugin, which registers resources and systems.
    template <typename P>
        requires PluginLike<std::remove_cvref_t<P>>
    App&
    add_plugin(P&& plugin) { // NOLINT(cppcoreguidelines-missing-std-forward): used as an lvalue
        if constexpr (requires { plugin.build(*this); }) {
            plugin.build(*this);
        } else {
            plugin(*this);
        }
        return *this;
    }

    App& add_system(Stage stage, System system, std::string name = {}, Order order = {}) {
        schedule_.add_system(stage, std::move(system), std::move(name), std::move(order));
        return *this;
    }

    template <typename R>
    App& insert_resource(R&& resource) {
        world_.insert_resource(std::forward<R>(resource));
        return *this;
    }

    /// Records that a module is installed. A module's generated `install`
    /// calls this before registering its systems. Problems (a dependency that
    /// is not installed yet, the same module twice) are listed by
    /// `module_problems`, so a game can check them once at startup.
    App& add_module(const ModuleInfo& info);

    [[nodiscard]] bool has_module(std::string_view name) const noexcept;

    /// The installed modules, in installation order.
    [[nodiscard]] const std::vector<ModuleInfo>& modules() const noexcept { return modules_; }

    /// Names of the installed modules, in installation order.
    [[nodiscard]] std::vector<std::string> module_names() const;

    [[nodiscard]] const std::vector<std::string>& module_problems() const noexcept {
        return module_problems_;
    }

    /// Registers the event type `T`: stores an `Events<T>` resource and ends
    /// its generation once `domain` has run (after each fixed tick for
    /// `FixedUpdate`, after the stage's frame run otherwise).
    ///
    /// An event sent during tick N is readable by every system during tick N
    /// and tick N+1, then dropped. Simulation events should use the default
    /// `FixedUpdate` domain so they depend on ticks, never on frame timing;
    /// presentation events (sounds, particles) can use `Update`.
    template <typename T>
    App& add_event(Stage domain = Stage::FixedUpdate) {
        world_.emplace_resource<ecs::Events<T>>();
        add_stage_hook(domain, [](ecs::World& world) {
            if (auto* events = world.resource<ecs::Events<T>>(); events != nullptr) {
                events->update();
            }
        });
        return *this;
    }

    /// Sets the fixed tick rate (ticks per second, at least 1) and the matching
    /// `Time` fields. Call it before the first frame.
    void set_tick_rate(std::uint32_t ticks_per_second);

    /// Runs one frame during which `real_delta` of real time passed: as many
    /// fixed ticks as fit, then `Update` and `PostUpdate` once. At most
    /// `max_catch_up_ticks` ticks run per frame; time beyond that is dropped
    /// so a slow frame cannot snowball. The first call also runs `Startup`.
    void advance(std::chrono::nanoseconds real_delta);

    /// Runs one frame with exactly one fixed tick.
    void update();

    /// Runs `frames` frames in a row, one fixed tick each.
    void run_frames(std::uint32_t frames);

    /// Fixed ticks completed so far.
    [[nodiscard]] std::uint64_t tick_count() const noexcept;

    /// Most fixed ticks `advance` runs in one frame (default 8).
    void set_max_catch_up_ticks(std::uint32_t ticks) noexcept { max_catch_up_ticks_ = ticks; }

private:
    void run_startup_once();
    void begin_tick_input();
    void run_stage(Stage stage);
    void add_stage_hook(Stage stage, System hook);

    ecs::World world_;
    Schedule schedule_;
    std::array<std::vector<System>, stage_count> hooks_;
    std::vector<ModuleInfo> modules_;
    std::vector<std::string> module_problems_;
    std::uint64_t frames_ = 0;
    std::uint64_t step_ns_ = 0;
    std::uint64_t accumulator_ns_ = 0;
    std::uint32_t max_catch_up_ticks_ = 8;
    bool started_ = false;
};

} // namespace ge::app
