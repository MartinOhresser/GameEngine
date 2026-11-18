#pragma once

#include "ge/game/catalog.hpp"
#include "ge/json/json.hpp"
#include "ge/module/spec.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ge::game {

/// The values of one component in a game file: `fields` is a JSON object, and
/// `name` is "module.Component".
struct ComponentData {
    std::string name;
    json::Value fields;
};

/// A reusable set of components. `extends` names another prefab whose
/// components come first and can be overridden field by field.
struct PrefabData {
    std::string name;
    std::string extends;
    std::vector<ComponentData> components;
};

/// One entity of a scene: optionally built from a prefab, then adjusted by its own components.
struct EntityData {
    std::string name;
    std::string prefab;
    std::vector<ComponentData> components;
};

struct SceneData {
    std::string name;
    std::vector<EntityData> entities;
};

struct ParamSetting {
    std::string module;
    std::string name;
    json::Value value;
};

/// The game's `game.json`.
struct Manifest {
    std::string name;
    std::string description;
    std::vector<std::string> modules;
    std::uint32_t tick_rate = 60;
    std::uint64_t seed = 0;
    std::vector<ParamSetting> params;
    std::vector<std::string> prefab_files;
    std::vector<std::string> scene_files;
    std::string start_scene;
};

/// A whole game: its manifest and the prefabs and scenes the manifest lists.
struct Game {
    Manifest manifest;
    std::vector<PrefabData> prefabs;
    std::vector<SceneData> scenes;

    [[nodiscard]] const PrefabData* find_prefab(std::string_view name) const;
    [[nodiscard]] const SceneData* find_scene(std::string_view name) const;
};

/// Returns the text of a file, or nothing when it cannot be read. Paths are
/// what the manifest says, joined to the manifest's directory with `/`.
using FileReader = std::function<std::optional<std::string>(const std::string& path)>;

struct LoadedGame {
    Game game;
    std::vector<module::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

/// Reads the manifest at `manifest_path` and every prefab and scene it lists,
/// and checks them against what the modules offer: modules and their
/// dependencies, components and fields, value types and bounds, references
/// between files, prefab chains and names. Reports every problem found, each
/// with its file and the place in it.
[[nodiscard]] LoadedGame load_game(const std::string& manifest_path, const FileReader& read,
                                   const Catalog& catalog);

/// The components of an entity once its prefab chain is applied: the base
/// prefab's first, each level overriding the one before field by field, then
/// `own`. `prefab` may be empty. The game must have been validated.
[[nodiscard]] std::vector<ComponentData> resolve_components(const Game& game,
                                                            std::string_view prefab,
                                                            const std::vector<ComponentData>& own);

} // namespace ge::game
