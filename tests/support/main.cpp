#include "mini_test.hpp"

#include <cstdio>
#include <cstring>

// Usage: ge_tests [--list] [--filter substring]
int main(int argc, char** argv) {
    const char* filter = nullptr;
    bool list_only = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--list") == 0) {
            list_only = true;
        } else if (std::strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
            filter = argv[++i];
        }
    }

    int run = 0;
    int failed = 0;
    for (const auto& test : mini_test::registry()) {
        if (filter != nullptr && std::strstr(test.name, filter) == nullptr) {
            continue;
        }
        if (list_only) {
            std::printf("%s\n", test.name);
            continue;
        }
        const int before = mini_test::failure_count();
        test.function();
        const bool ok = mini_test::failure_count() == before;
        std::printf("[%s] %s\n", ok ? " ok " : "FAIL", test.name);
        ++run;
        if (!ok) {
            ++failed;
        }
    }

    if (list_only) {
        return 0;
    }
    std::printf("%d tests, %d failed\n", run, failed);
    // An empty selection is an error: it usually means a wrong filter.
    return (failed == 0 && run > 0) ? 0 : 1;
}
