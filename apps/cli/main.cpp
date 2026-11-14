// SPDX-License-Identifier: Apache-2.0
#include <rse/core/machine.hpp>
#include <iostream>
#include <string_view>
int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        std::cout << "Retro Silicon Emulator 0.1.0 (scaffold)\n";
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "Usage: rse-cli [--help|--version]\n";
        return 0;
    }
    std::cerr << "NES emulation is not implemented yet. Use --help.\n";
    return 2;
}
