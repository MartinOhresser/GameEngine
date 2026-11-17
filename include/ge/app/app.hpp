#pragma once

#include "ge/ecs/world.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ge::app {

/// Phases of a frame, in execution order.
///
/// `Startup` runs once, before the first frame. The other three run every
/// frame in the order `PreUpdate`, `Update`, `PostUpdate`.
enum class Stage : std::uint8_t {
    Startup,    ///< Once, before the first frame.
    PreUpdate,  ///< Gather input, advance clocks.
    Update,     ///< Gameplay logic.
    PostUpdate, ///< Reconcile and clean up.
};

/// Any callable taking the world is a system.
using System = std::function<void(ecs::World&)>;

/// Systems grouped by stage. Within a stage they run in registration order.
class Schedule {
public:
    /// Adds a system. `name` shows up in `system_names`; it defaults to
    /// `system_<n>`, where n is the system's position in the stage.
    void add_system(Stage stage, System system, std::string name = {});

    /// Runs every system of one stage.
    void run_stage(Stage stage, ecs::World& world);

    [[nodiscard]] std::size_t system_count(Stage stage) const noexcept;

    /// Names of the systems of a stage, in execution order.
    [[nodiscard]] std::vector<std::string> system_names(Stage stage) const;

private:
    struct Entry {
        std::string name;
        System run;
    };

    static constexpr std::size_t stage_count = 4;

    std::array<std::vector<Entry>, stage_count> stages_;
};

class App;

/// A plugin is an object with `void build(App&)`, or any callable taking `App&`.
template <typename P>
concept PluginLike = requires(P& plugin, App& app) { plugin.build(app); } ||
                     requires(P& plugin, App& app) { plugin(app); };

/// A world, its schedule and the frame loop.
class App {
public:
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

    App& add_system(Stage stage, System system, std::string name = {}) {
        schedule_.add_system(stage, std::move(system), std::move(name));
        return *this;
    }

    template <typename R>
    App& insert_resource(R&& resource) {
        world_.insert_resource(std::forward<R>(resource));
        return *this;
    }

    /// Runs one frame. The first call also runs the `Startup` systems.
    void update();

    /// Runs `frames` frames in a row.
    void run_frames(std::uint32_t frames);

private:
    ecs::World world_;
    Schedule schedule_;
    std::uint64_t frames_ = 0;
    bool started_ = false;
};

} // namespace ge::app
