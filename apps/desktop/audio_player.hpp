// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <filesystem>
#include <string_view>
// Audio-only cartridge playback; no video presentation is implied.
int play_audio(const std::filesystem::path& cartridge,unsigned seconds,std::string_view system="nes-ntsc");
