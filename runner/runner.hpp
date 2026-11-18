#pragma once

#include "ge/game/runtime.hpp"

#include <iosfwd>
#include <span>
#include <string>
#include <vector>

namespace ge::runner {

/// `gerun GAME.json [--ticks N]`: validates a game, starts it, and runs it
/// without a window for N ticks (default 60). Prints the number of entities
/// and a fingerprint of the final state: the same game always prints the same.
///
/// Exit codes: 0 success, 1 the game is invalid or could not start, 2 usage.
int run_gerun(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

/// Same, with an explicit list of linked modules (for tests).
int run_gerun(const std::vector<std::string>& args, std::span<const game::ModuleEntry> modules,
              std::ostream& out, std::ostream& err);

} // namespace ge::runner
