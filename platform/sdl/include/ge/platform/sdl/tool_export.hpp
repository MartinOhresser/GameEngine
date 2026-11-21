#pragma once

// The export service of `gelauncher`: `gectl export` run in the background, in a thread of the
// launcher's own program, so that the launcher keeps drawing and answering while a zip is made.
// The platforms are those of `gectl export --targets`.

#include "ge/platform/sdl/launcher.hpp"

#include "export.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ge::platform::sdl {

struct ToolExportOptions {
    /// The engine's folder, where the program of the game is built.
    std::string source_dir = ".";
    /// Where the zips go.
    std::string dist_dir = "dist";
    /// Also offer the platforms that exist only for tests.
    bool include_hidden = false;
    /// How the commands of an export run; the default runs them for real, with their output
    /// handed to the launcher. Tests pass one that pretends.
    tools::CommandRunner runner;
};

/// An export service that runs `gectl export` as a background thread. One export at a time.
/// Closing the program while an export runs leaves the thread to end with the program.
[[nodiscard]] std::unique_ptr<ExportService> make_tool_export(const ToolExportOptions& options);

} // namespace ge::platform::sdl
