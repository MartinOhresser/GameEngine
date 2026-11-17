#include "gectl.hpp"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    const std::vector<std::string> args(argv + 1, argv + argc);
    return ge::tools::run_gectl(args, std::cout, std::cerr);
}
