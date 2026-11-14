// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <rse/systems/nes/cartridge.hpp>
#include <rse/systems/nes/cpu_bus.hpp>
#include <array>
#include <cstddef>
#include <cstdint>

namespace rse::nes {
// Owns a cartridge and zero-initialized CPU/nametable RAM. Unconnected devices
// return zero; no bus timing or register side effects yet. See NES_MEMORY.md.
class Bus : public CpuBus {
public:
    explicit Bus(Cartridge cartridge);
    // CPU-cycle interface; memory mapping does not itself advance time.
    std::uint8_t read(std::uint16_t address, std::uint64_t) noexcept override {
        return cpu_read(address);
    }
    void write(std::uint16_t address, std::uint8_t value, std::uint64_t) noexcept override {
        cpu_write(address, value);
    }
    [[nodiscard]] std::uint8_t cpu_read(std::uint16_t address) noexcept;
    void cpu_write(std::uint16_t address, std::uint8_t value) noexcept;
    // PPU addresses wrap at 14 bits. Palette storage is deferred to the PPU.
    [[nodiscard]] std::uint8_t ppu_read(std::uint16_t address) noexcept;
    void ppu_write(std::uint16_t address, std::uint8_t value) noexcept;
private:
    [[nodiscard]] std::size_t nametable_index(std::uint16_t address) const noexcept;
    Cartridge cartridge_;
    std::array<std::uint8_t, 2048> ram_{};
    std::array<std::uint8_t, 2048> nametables_{};
};
} // namespace rse::nes
