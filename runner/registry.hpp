#pragma once

#include "ge/game/runtime.hpp"

#include <span>

namespace ge::runner {

/// Every module of the repository, linked into this program.
[[nodiscard]] std::span<const game::ModuleEntry> linked_modules();

} // namespace ge::runner
