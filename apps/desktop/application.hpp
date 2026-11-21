// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "trainer_config.hpp"
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <rse/session/session.hpp>
inline std::filesystem::path utf8_path(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(),text.end()));
}
using DesktopLoad=std::function<std::unique_ptr<rse::session::Session>(const std::filesystem::path&)>;
using DesktopFinish=std::function<void(const rse::session::Session&)>;
struct DesktopWorkspace {
    std::optional<rse::desktop::TrainerConfig> trainer;
    std::filesystem::path profile;
    std::string observation;
    bool visible{},autostart{};
};
int run_desktop(const std::filesystem::path& cartridge,unsigned seconds,bool smoke,
                std::string_view system="nes-ntsc",DesktopLoad loader={},DesktopFinish finish={},
                DesktopWorkspace workspace={});
