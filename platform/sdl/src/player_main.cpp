// The entry point of an exported game. It is built once per game, with the registry of that
// game's modules (see `ge_add_player`), and is a GUI program on Windows: no console, so what
// went wrong is shown in a dialog box.

#include "ge/platform/sdl/player.hpp"
#include "ge/platform/sdl/sdl_backend.hpp"

#include "registry.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    std::ostringstream err;
    const int code =
        ge::platform::sdl::run_player(args, ge::runner::linked_modules(), std::cout, err);
    if (code != 0) {
        std::cerr << err.str();
        ge::platform::sdl::show_error("The game cannot start", err.str());
    }
    return code;
}
