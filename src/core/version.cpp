#include "ge/core/version.hpp"

namespace ge {

std::string_view version() noexcept {
    return GE_VERSION_STRING;
}

} // namespace ge
