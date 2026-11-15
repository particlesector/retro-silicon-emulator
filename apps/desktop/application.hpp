// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <filesystem>
#include <string_view>
inline std::filesystem::path utf8_path(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(),text.end()));
}
int run_desktop(const std::filesystem::path& cartridge,unsigned seconds,bool smoke);
