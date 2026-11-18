#pragma once

#include "ge/game/catalog.hpp"
#include "ge/json/json.hpp"

#include <string>
#include <vector>

namespace ge::game {

struct SchemaFile {
    /// File name, e.g. "game.schema.json".
    std::string path;
    json::Value schema;
};

/// JSON Schemas for the files of a game (`game.schema.json`, `prefab.schema.json`
/// and `scene.schema.json`), generated from what the modules offer. Editors such
/// as VS Code use them to complete module, component and field names and to flag
/// wrong types and out-of-range values while the file is being typed. The
/// schemas describe the same rules `load_game` enforces, minus the checks that
/// span several files.
[[nodiscard]] std::vector<SchemaFile> editor_schemas(const Catalog& catalog);

} // namespace ge::game
