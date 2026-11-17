#pragma once

#include <string_view>

namespace ge {

/// Engine version as `major.minor.patch`, taken from the CMake project version.
[[nodiscard]] std::string_view version() noexcept;

} // namespace ge
