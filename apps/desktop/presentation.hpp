// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "session_output.hpp"
#include <algorithm>
#include <array>
#include <cstdint>

namespace rse::desktop {
using Rgb=session::Rgba;
struct Viewport {int x{},y{},w{},h{};bool operator==(const Viewport&) const=default;};
inline Viewport viewport(int width,int height,session::Rational aspect={4,3}) noexcept {
    if (width<=0 || height<=0 || !aspect.numerator || !aspect.denominator) return {};
    const auto ratio=static_cast<long double>(aspect.numerator)/aspect.denominator;
    const auto w=int(std::min<long double>(width,height*ratio));
    const auto h=int(std::min<long double>(height,w/ratio));return {(width-w)/2,(height-h)/2,w,h};
}
class Input {
public:
    void key(std::uint16_t mask,bool down) noexcept {
        if (!focused_) return;
        if (down) keys_|=mask;else keys_&=std::uint16_t(~mask);
    }
    void gamepad(std::uint16_t buttons) noexcept {pad_=buttons;}
    void focus(bool value) noexcept {focused_=value;if (!value) keys_=0;}
    void clear() noexcept {keys_=pad_=0;}
    [[nodiscard]] std::uint16_t buttons() const noexcept {return focused_ ? keys_|pad_ : 0;}
private:
    std::uint16_t keys_{},pad_{};bool focused_{true};
};
} // namespace rse::desktop
