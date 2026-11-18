#pragma once

#include "ge/game/catalog.hpp"
#include "ge/json/json.hpp"
#include "ge/module/spec.hpp"
#include "ge/platform/input_map.hpp"

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

enum class Compare : std::uint8_t { Eq, Ne, Lt, Le, Gt, Ge };

enum class CheckKind : std::uint8_t {
    Field,       ///< "hero.transform2d.Position.x > 100"
    EntityCount, ///< "entities == 3"
    Alive,       ///< "hero alive"
    Gone,        ///< "spark gone"
    Fingerprint, ///< "fingerprint == 5f4962bd950cf9b3"
};

/// One thing a scenario expects to be true at a given tick.
struct Check {
    /// The number of ticks that have run when the check is made.
    std::uint64_t at = 0;
    CheckKind kind = CheckKind::Field;
    /// The check as it was written, for reports.
    std::string text;
    std::string entity;
    std::string component; ///< "module.Component"
    std::string field;
    Compare compare = Compare::Eq;
    /// The number or boolean a field is compared to, or the count.
    json::Value value;
    std::uint64_t fingerprint = 0;
};

/// A change of an action at a tick: the scripted "player".
struct ScenarioInput {
    std::uint64_t tick = 0;
    std::string action;
    float value = 1.0F;
};

/// A scripted run of the game with expectations: "after 300 ticks, hero's
/// score is above 0". It starts its `scene`, or the game's `start_scene`.
struct ScenarioData {
    std::string name;
    std::string description;
    /// The scene it starts; empty means the game's `start_scene`.
    std::string scene;
    std::uint64_t ticks = 0;
    std::vector<ScenarioInput> inputs;
    std::vector<Check> checks;
};

struct ParamSetting {
    std::string module;
    std::string name;
    json::Value value;
};

/// A picture cut into equal cells: the sprites of a game come from it.
/// Cells are numbered row by row from the top left, starting at 0.
struct AtlasData {
    std::string name;
    /// Path of the image file, as the file reader understands it.
    std::string image;
    int cell_width = 0;
    int cell_height = 0;
    /// For a font: the characters the cells show, in cell order (UTF-8).
    std::string glyphs;
};

/// The words of one language: `entries` in file order, key then text.
struct StringsData {
    std::string locale;
    std::vector<std::pair<std::string, std::string>> entries;
};

/// The game's `game.json`.
struct Manifest {
    std::string name;
    std::string description;
    std::vector<std::string> modules;
    std::uint32_t tick_rate = 60;
    std::uint64_t seed = 0;
    /// The size in pixels of the area the game draws in (`window`).
    std::uint32_t window_width = 800;
    std::uint32_t window_height = 600;
    std::vector<ParamSetting> params;
    /// The game's own named lists, in the order the manifest gives them: the
    /// names that `ref` and `ref_set` fields refer to (item kinds, categories...).
    std::vector<std::pair<std::string, std::vector<std::string>>> lists;
    /// Files of words, one per language; the first one's keys make the game's
    /// `string` list. `locale` picks the one in use (the first when empty).
    std::vector<std::string> strings_files;
    std::string locale;
    std::vector<std::string> atlas_files;
    std::vector<std::string> prefab_files;
    std::vector<std::string> scene_files;
    std::vector<std::string> scenario_files;
    /// The file that binds keys and buttons to actions; empty if the game has none.
    std::string input_file;
    std::string start_scene;
};

/// A whole game: its manifest and the prefabs, scenes and scenarios the manifest lists.
struct Game {
    Manifest manifest;
    std::vector<PrefabData> prefabs;
    std::vector<SceneData> scenes;
    std::vector<ScenarioData> scenarios;
    /// In the order of the manifest: an atlas's position is the index a u32
    /// field with `"ref": "atlas"` holds.
    std::vector<StringsData> strings;
    std::vector<AtlasData> atlases;
    /// What the `input` file binds; empty when the manifest names none.
    platform::InputMap input;

    [[nodiscard]] const PrefabData* find_prefab(std::string_view name) const;
    [[nodiscard]] const SceneData* find_scene(std::string_view name) const;
    [[nodiscard]] const ScenarioData* find_scenario(std::string_view name) const;

    /// The names that a u32 field with `"ref": <ref>` can use, in index order;
    /// empty when the game has no such list.
    [[nodiscard]] std::vector<std::string> ref_names(std::string_view ref) const;

    /// The words of the language the manifest picks, or null when the game has none.
    [[nodiscard]] const StringsData* active_strings() const;
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
