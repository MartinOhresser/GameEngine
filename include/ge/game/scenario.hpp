#pragma once

#include "ge/game/game.hpp"
#include "ge/game/runtime.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ge::game {

/// Reads the text of a check ("hero.transform2d.Position.x > 100"). Returns an
/// error message, or an empty string when `out` was filled in. It only checks
/// the shape; what the names refer to is checked against the game.
[[nodiscard]] std::string parse_check(const std::string& text, Check& out);

/// A fingerprint as 16 lowercase hexadecimal digits, the form checks are written in.
[[nodiscard]] std::string format_fingerprint(std::uint64_t fingerprint);

struct ScenarioReport {
    std::string name;
    /// What went wrong, one line each; empty when the scenario passed.
    std::vector<std::string> failures;
    /// The world fingerprint after the last tick.
    std::uint64_t fingerprint = 0;

    [[nodiscard]] bool passed() const noexcept { return failures.empty(); }
};

/// Runs a scenario headless: starts the game, feeds the scripted inputs, runs
/// the ticks and evaluates the checks. The scenario then runs a second time
/// and must end in the same state, which is how a loss of determinism shows up.
[[nodiscard]] ScenarioReport run_scenario(const Game& game, const ScenarioData& scenario,
                                          std::span<const ModuleEntry> modules);

} // namespace ge::game
