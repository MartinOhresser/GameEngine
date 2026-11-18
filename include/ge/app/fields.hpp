#pragma once

#include "ge/app/module.hpp"

#include <string>
#include <vector>

namespace ge::app {

/// One field of one component, with the qualified name a game file uses for it:
/// "module.Component.field".
struct FieldEntry {
    std::string name;
    const ComponentInfo* component = nullptr;
    std::string field;
};

/// Every field of the installed modules, sorted by qualified name. A field with
/// `"ref": "field"` holds an index into it, and the game's own `field` list
/// (see `Game::ref_names`) has the same names in the same order: that is how a
/// text or a bar shows a number of the game (a score, a life) without code.
struct FieldTable {
    std::vector<FieldEntry> entries;
};

} // namespace ge::app
