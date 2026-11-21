// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <filesystem>
#include <string>

namespace rse::desktop {
struct TrainerConfig {
    std::filesystem::path python,script,bridge,output;
    std::string device{"cpu"};
    unsigned steps{16'384};
};
}
