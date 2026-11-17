#pragma once

#include "ge/app/app.hpp"
#include "ge/game/game.hpp"

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ge::game {

/// A module that is linked into the program: what its generated code offers.
struct ModuleEntry {
    const app::ModuleInfo& (*info)();
    void (*install)(app::App&);
};

/// The catalog of the linked modules, for validating a game inside the program.
[[nodiscard]] Catalog catalog_of(std::span<const ModuleEntry> modules);

struct SetupResult {
    /// The named entities of the scene, in file order.
    std::vector<std::pair<std::string, ecs::Entity>> named;
    /// Anything that went wrong; empty when the game is ready to run.
    std::vector<std::string> problems;

    [[nodiscard]] bool ok() const noexcept { return problems.empty(); }

    /// The entity with this name in the scene, or a null entity.
    [[nodiscard]] ecs::Entity find(std::string_view name) const;
};

/// Prepares `app` to run a validated game: sets the tick rate and the seed,
/// installs the modules the manifest lists (dependencies first, whatever order
/// the manifest gives), applies the manifest's params, and creates the entities
/// of the start scene.
[[nodiscard]] SetupResult setup_game(app::App& app, const Game& game,
                                     std::span<const ModuleEntry> available);

/// Creates the entities of a scene, with the prefabs applied. The modules the
/// game uses must be installed in `app`.
[[nodiscard]] SetupResult spawn_scene(app::App& app, const Game& game, std::string_view scene);

} // namespace ge::game
