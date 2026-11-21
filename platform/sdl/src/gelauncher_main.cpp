#include "ge/platform/sdl/launcher.hpp"

#include "registry.hpp"

#ifdef GE_LAUNCHER_EXPORT
#include "ge/platform/sdl/tool_export.hpp"
#endif

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    ge::platform::sdl::ExporterFactory exporter;
#ifdef GE_LAUNCHER_EXPORT
    exporter = [](const std::string& source, const std::string& dist) {
        ge::platform::sdl::ToolExportOptions options;
        options.source_dir = source;
        options.dist_dir = dist;
        return ge::platform::sdl::make_tool_export(options);
    };
#endif
    return ge::platform::sdl::run_gelauncher(args, ge::runner::linked_modules(), std::cout,
                                             std::cerr, exporter);
}
