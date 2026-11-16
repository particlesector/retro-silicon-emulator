// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <rse/video/rp2c02.hpp>
#include <rse/systems/nes/palette.hpp>
#include <algorithm>
#include <array>
#include <cstdint>

namespace rse::desktop {
using Rgb=session::Rgba;
inline Rgb rgb(std::uint16_t sample) noexcept {return nes::display_color(sample);}
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
