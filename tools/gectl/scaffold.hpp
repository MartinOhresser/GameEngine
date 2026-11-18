#pragma once

#include <string>
#include <vector>

namespace ge::tools {

struct ScaffoldFile {
    /// Relative to the module's directory.
    std::string path;
    std::string content;
};

/// The files of a new module: a `module.json` with one example component and
/// system, the system's code, and a test that runs it. The module builds and
/// passes its test as it is; the example is meant to be replaced.
[[nodiscard]] std::vector<ScaffoldFile> scaffold_module(const std::string& name,
                                                        const std::vector<std::string>& depends);

} // namespace ge::tools
