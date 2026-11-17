#pragma once

// A deliberately tiny test framework: no dependency to download, so the test
// suite builds the same way on desktop, WebAssembly, Android and iOS CI jobs.
// It can be swapped for doctest or Catch2 later without touching the engine.

#include <cstdio>
#include <vector>

namespace mini_test {

struct Case {
    const char* name;
    void (*function)();
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failure_count() {
    static int count = 0;
    return count;
}

struct Registrar {
    Registrar(const char* name, void (*function)()) { registry().push_back({name, function}); }
};

inline void report_failure(const char* file, int line, const char* expression) {
    std::fprintf(stderr, "  %s:%d: check failed: %s\n", file, line, expression);
    ++failure_count();
}

} // namespace mini_test

#define MT_CONCAT_IMPL(a, b) a##b
#define MT_CONCAT(a, b) MT_CONCAT_IMPL(a, b)

/// Declares and registers a test case: `TEST_CASE("name") { ... }`.
#define TEST_CASE(name)                                                                            \
    namespace {                                                                                    \
    struct MT_CONCAT(MtTest, __LINE__) {                                                           \
        static void run();                                                                         \
    };                                                                                             \
    const mini_test::Registrar MT_CONCAT(mt_registrar_,                                            \
                                         __LINE__)(name, &MT_CONCAT(MtTest, __LINE__)::run);       \
    }                                                                                              \
    void MT_CONCAT(MtTest, __LINE__)::run()

/// Records a failure and keeps running the test.
#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            mini_test::report_failure(__FILE__, __LINE__, #expression);                            \
        }                                                                                          \
    } while (false)

/// Records a failure and returns from the enclosing void function.
#define REQUIRE(expression)                                                                        \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            mini_test::report_failure(__FILE__, __LINE__, #expression);                            \
            return;                                                                                \
        }                                                                                          \
    } while (false)
