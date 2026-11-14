// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>

namespace rse::video {
// Cartridge-side PPU memory: 14-bit addresses, completion timestamp in PPU dots.
// Palette and OAM are chip-local. Callbacks must not reenter the PPU.
class PpuBus {
public:
    virtual ~PpuBus() = default;
    virtual std::uint8_t read(std::uint16_t address, std::uint64_t dot) = 0;
    virtual void write(std::uint16_t address, std::uint8_t value, std::uint64_t dot) = 0;
};
} // namespace rse::video
