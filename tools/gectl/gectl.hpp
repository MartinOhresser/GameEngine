#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace ge::tools {

/// Runs one `gectl` command line (without the program name) and returns the
/// process exit code: 0 on success, 1 when the files are invalid or stale,
/// 2 for a usage error.
[[nodiscard]] int run_gectl(const std::vector<std::string>& args, std::ostream& out,
                            std::ostream& err);

} // namespace ge::tools
