#pragma once

#include "ge/module/spec.hpp"

#include <string>
#include <vector>

namespace ge::module {

struct GeneratedFile {
    /// Path relative to the module's `gen` directory, with `/` separators.
    std::string path;
    std::string content;
};

/// Produces the C++ for a module from its `module.json`: the component
/// structs, the system declarations the module must implement, `info()` and
/// `install()`. The output is deterministic, so a committed copy can be
/// compared with a fresh run to catch a forgotten regeneration.
[[nodiscard]] std::vector<GeneratedFile> generate(const ModuleSpec& module);

} // namespace ge::module
