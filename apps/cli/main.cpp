// SPDX-License-Identifier: Apache-2.0
#include "commands.hpp"
#include <iostream>
#include <string_view>
#include <vector>
int main(int argc, char** argv) {
    const std::vector<std::string_view> arguments(argv+1,argv+argc);
    return rse::cli::run(arguments,std::cout,std::cerr);
}
