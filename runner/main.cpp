#include "runner.hpp"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    return ge::runner::run_gerun(args, std::cout, std::cerr);
}
