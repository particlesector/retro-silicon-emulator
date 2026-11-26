// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace rse::gba {

inline constexpr unsigned screen_width = 240, screen_height = 160;
inline constexpr unsigned line_ticks = 1232, hblank_tick = 1006, frame_lines = 228;
inline constexpr unsigned first_frame_tick = line_ticks * screen_height;
inline constexpr unsigned frame_ticks = line_ticks * frame_lines;

// Scanline renderer for the GBA LCD controller. The board supplies the
// currently visible register and memory values and publishes at VBlank entry.
class Display {
public:
    using Frame = std::array<std::uint16_t, screen_width * screen_height>; // BGR555.
    using Registers = std::span<const std::uint16_t, 0x200>;
    using Vram = std::span<const std::uint8_t, 0x18000>;
    using Palette = std::span<const std::uint8_t, 0x400>;
    using Oam = std::span<const std::uint8_t, 0x400>;

    void reset() noexcept;
    void write_reference(unsigned address, Registers regs) noexcept;
    void begin_frame(Registers regs) noexcept;
    void draw(unsigned y, Registers regs, Vram vram, Palette palette, Oam oam);
    void publish() noexcept;
    std::uint64_t number() const noexcept { return number_; }
    const Frame& frame() const noexcept { return frames_[published_]; }

private:
    std::array<Frame, 2> frames_{};
    std::array<std::int32_t, 4> references_{};
    std::uint64_t number_{};
    unsigned published_{};
};

}
