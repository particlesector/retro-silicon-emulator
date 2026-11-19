// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <rse/video/ppu_bus.hpp>
#include <rse/core/state_codec.hpp>
#include <array>
#include <memory>

namespace rse::video {
struct PpuFrame {
    static constexpr unsigned width = 256, height = 240;
    // Bits 0-5: palette color; bits 6-8: emphasis. Not display RGB.
    std::array<std::uint16_t, width * height> samples{};
    std::uint64_t number{};
};
struct PpuState {
    std::uint16_t scanline{}, dot{}, v{}, t{};
    std::uint8_t fine_x{}, control{}, mask{}, status{}, oam_address{};
    bool write_toggle{}, odd{}, nmi{}, faulted{};
    std::uint64_t dots{}, frames{};
};

// NTSC RP2C02 dot-stepped model. No machine wiring or frontend dependencies.
// See docs/PPU_2C02.md for access phase, initialization, and accuracy boundaries.
class Rp2c02 {
public:
    explicit Rp2c02(PpuBus& bus);
    ~Rp2c02();
    Rp2c02(const Rp2c02&) = delete;
    Rp2c02& operator=(const Rp2c02&) = delete;
    void clock();
    // New deterministic timeline; external bus memory is preserved.
    void reset();
    // Native chip select (low 3 bits). Accesses do not advance time.
    [[nodiscard]] std::uint8_t cpu_read(std::uint16_t select);
    [[nodiscard]] std::uint8_t cpu_peek(std::uint16_t select) const noexcept;
    [[nodiscard]] std::uint8_t inspect_palette(std::uint16_t address) const noexcept;
    [[nodiscard]] std::uint8_t inspect_oam(std::uint8_t address) const noexcept;
    void cpu_write(std::uint16_t select, std::uint8_t value);
    [[nodiscard]] bool nmi_asserted() const noexcept;
    // Non-mutating diagnostics, not a serialized save-state format.
    [[nodiscard]] PpuState state() const noexcept;
    // Completed frame, valid until next publication/reset. Initial frame is zero.
    [[nodiscard]] const PpuFrame& frame() const noexcept;
    void save_state(rse::state::Writer&) const;
    void load_state(rse::state::Reader&);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace rse::video
