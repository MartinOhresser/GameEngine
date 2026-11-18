#pragma once

#include "check.hpp"

namespace ge::game::detail {

/// Reads a scenario file's contents; reports problems with the file's name.
std::optional<ScenarioData> read_scenario(const Context& context, const std::string& file,
                                          const json::Value& root);

/// Checks what the scenarios refer to: unique names, entities of the start
/// scene, components and fields of the manifest's modules. `files[i]` is the
/// file of `game.scenarios[i]`.
void check_scenario_links(const Context& context, const Game& game,
                          const std::vector<std::string>& files);

} // namespace ge::game::detail
