// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <rse/chips/cd4021.hpp>

namespace rse::devices {
// Standard NES controller wiring. Button bits 0..7 are A,B,Select,Start,
// Up,Down,Left,Right. Pressed buttons pull parallel pins low; serial input is
// grounded. read() returns raw DATA; the console inverts it at the CPU port.
class NesController {
public:
    NesController() {
        shift_.set_parallel(0xff);
        shift_.set_load(true); shift_.set_load(false);
    }
    void set_buttons(std::uint8_t buttons) noexcept {
        buttons_ = buttons;
        std::uint8_t pins = 0;
        for (unsigned bit = 0; bit < 8; ++bit)
            pins |= ((buttons >> bit) & 1) << (7 - bit);
        shift_.set_parallel(static_cast<std::uint8_t>(~pins));
    }
    void strobe(bool high) noexcept { shift_.set_load(high); }
    [[nodiscard]] std::uint8_t read() noexcept {
        const auto value = static_cast<std::uint8_t>(shift_.output());
        shift_.clock();
        return value;
    }
    [[nodiscard]] std::uint8_t buttons() const noexcept { return buttons_; }
    [[nodiscard]] chips::Cd4021State state() const noexcept { return shift_.state(); }
private:
    chips::Cd4021 shift_;
    std::uint8_t buttons_{};
};
} // namespace rse::devices
