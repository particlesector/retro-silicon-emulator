// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <rse/video/rp2c02.hpp>
#include <algorithm>
#include <array>
#include <cstdint>

namespace rse::desktop {
struct Rgb {std::uint8_t r{},g{},b{},a{255};bool operator==(const Rgb&) const=default;};
// Conventional 64-color NTSC display approximation, not a composite decoder.
// Keep palette-code mapping explicit: hue spacing is not linear in RGB.
// Emphasis remains an approximation which attenuates other primaries.
inline Rgb rgb(std::uint16_t sample) noexcept {
    constexpr std::array<std::uint32_t,64> palette{
        0x7c7c7c,0x0000fc,0x0000bc,0x4428bc,0x940084,0xa80020,0xa81000,0x881400,
        0x503000,0x007800,0x006800,0x005800,0x004058,0x000000,0x000000,0x000000,
        0xbcbcbc,0x0078f8,0x0058f8,0x6844fc,0xd800cc,0xe40058,0xf83800,0xe45c10,
        0xac7c00,0x00b800,0x00a800,0x00a844,0x008888,0x000000,0x000000,0x000000,
        0xf8f8f8,0x3cbcfc,0x6888fc,0x9878f8,0xf878f8,0xf85898,0xf87858,0xfca044,
        0xf8b800,0xb8f818,0x58d854,0x58f898,0x00e8d8,0x787878,0x000000,0x000000,
        0xfcfcfc,0xa4e4fc,0xb8b8f8,0xd8b8f8,0xf8b8f8,0xf8a4c0,0xf0d0b0,0xfce0a8,
        0xf8d878,0xd8f878,0xb8f8b8,0xb8f8d8,0x00fcfc,0xb8b8b8,0x000000,0x000000};
    const auto color=palette[sample&63];
    int r=(color>>16)&255,g=(color>>8)&255,b=color&255;
    if (sample&64) {g=g*3/4;b=b*3/4;}
    if (sample&128) {r=r*3/4;b=b*3/4;}
    if (sample&256) {r=r*3/4;g=g*3/4;}
    return {std::uint8_t(r),std::uint8_t(g),std::uint8_t(b),255};
}
inline auto pixels(const video::PpuFrame& frame) noexcept {
    std::array<Rgb,video::PpuFrame::width*video::PpuFrame::height> output;
    std::transform(frame.samples.begin(),frame.samples.end(),output.begin(),rgb);return output;
}
struct Viewport {int x{},y{},w{},h{};bool operator==(const Viewport&) const=default;};
inline Viewport viewport(int width,int height) noexcept {
    if (width<=0 || height<=0) return {};
    const auto w=std::min<std::int64_t>(width,std::int64_t(height)*4/3);
    const auto h=w*3/4;return {(width-int(w))/2,(height-int(h))/2,int(w),int(h)};
}
class Input {
public:
    void key(std::uint8_t mask,bool down) noexcept {
        if (!focused_) return;
        if (down) keys_|=mask;else keys_&=std::uint8_t(~mask);
    }
    void gamepad(std::uint8_t buttons) noexcept {pad_=buttons;}
    void focus(bool value) noexcept {focused_=value;if (!value) keys_=0;}
    void clear() noexcept {keys_=pad_=0;}
    [[nodiscard]] std::uint8_t buttons() const noexcept {return focused_ ? keys_|pad_ : 0;}
private:
    std::uint8_t keys_{},pad_{};bool focused_{true};
};
} // namespace rse::desktop
