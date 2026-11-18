#include "ge/platform/sdl/geplay.hpp"

#include "registry.hpp"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    return ge::platform::sdl::run_geplay(args, ge::runner::linked_modules(), std::cout, std::cerr);
}
