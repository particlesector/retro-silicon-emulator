// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <rse/systems/nes/bus.hpp>
#include <rse/video/ppu_bus.hpp>

namespace rse::nes {
// Machine-owned wiring between the reusable PPU and NROM/nametable memory.
// Memory must outlive this adapter, and the adapter must outlive its PPU.
class PpuMemory final : public video::PpuBus {
public:
    explicit PpuMemory(Bus& memory) : memory_(memory) {}
    std::uint8_t read(std::uint16_t address, std::uint64_t) noexcept override {
        return memory_.ppu_read(address);
    }
    void write(std::uint16_t address, std::uint8_t value, std::uint64_t) noexcept override {
        memory_.ppu_write(address, value);
    }
private:
    Bus& memory_;
};
} // namespace rse::nes
