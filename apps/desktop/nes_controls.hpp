// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "session_output.hpp"
#include <array>

namespace rse::desktop {
// Default NES controls are application preferences, separate from playback.
inline std::vector<PortInput> nes_controls(std::uint8_t buttons) {
    constexpr std::array names{"a","b","select","start","up","down","left","right"};
    PortInput input{"controller-1",{}};
    for(unsigned i=0;i<names.size();++i)if(buttons&(1u<<i))input.pressed.emplace_back(names[i]);
    return {std::move(input)};
}
inline std::vector<PortInput> default_controls(std::string_view id,std::uint16_t buttons) {
    if (id!="nes-ntsc" && id!="gba") return {};
    auto input=nes_controls(std::uint8_t(buttons));
    if (id=="gba") {
        if(buttons&0x100)input.front().pressed.emplace_back("l");
        if(buttons&0x200)input.front().pressed.emplace_back("r");
    }
    return input;
}
}
