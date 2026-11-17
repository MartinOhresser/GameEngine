#pragma once

#include <cstdio>
#include <cstdlib>

namespace ge::detail {

// The abort path cannot be exercised without ending the test process.
// GCOVR_EXCL_START
[[noreturn]] inline void assertion_failed(const char* expression, const char* file,
                                          int line) noexcept {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): diagnostics only, no allocation.
    std::fprintf(stderr, "%s:%d: assertion failed: %s\n", file, line, expression);
    std::abort();
}
// GCOVR_EXCL_STOP

} // namespace ge::detail

/// Checks an internal invariant. Active unless `NDEBUG` is defined; in that
/// case the expression is still compiled (so it cannot rot) but not evaluated.
#ifndef NDEBUG
#define GE_ASSERT(condition)                                                                       \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            ::ge::detail::assertion_failed(#condition, __FILE__, __LINE__);                        \
        }                                                                                          \
    } while (false)
#else
#define GE_ASSERT(condition)                                                                       \
    do {                                                                                           \
        static_cast<void>(sizeof(condition));                                                      \
    } while (false)
#endif
