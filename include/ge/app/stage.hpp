#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ge::app {

/// Phases of a frame, in execution order.
///
/// `Startup` runs once, before the first frame. The others run every frame, in
/// the order `PreUpdate`, `FixedUpdate`, `Update`, `PostUpdate`.
enum class Stage : std::uint8_t {
    Startup,     ///< Once, before the first frame.
    PreUpdate,   ///< Gather input.
    FixedUpdate, ///< Simulation: runs zero or more times per frame, at a fixed rate.
    Update,      ///< Per-frame logic that does not belong to the simulation.
    PostUpdate,  ///< Reconcile and clean up.
};

/// Number of stages.
inline constexpr std::size_t stage_count = 5;

[[nodiscard]] std::string_view to_string(Stage stage) noexcept;

} // namespace ge::app
