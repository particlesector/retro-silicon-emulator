// SPDX-License-Identifier: Apache-2.0
#include <rse/systems/nes/bus.hpp>
#include <utility>

namespace rse::nes {
Bus::Bus(Cartridge cartridge) : cartridge_(std::move(cartridge)) {}

std::uint8_t Bus::cpu_read(std::uint16_t address) noexcept {
    if (address < 0x2000) return ram_[address & 0x07ff];
    return cartridge_.cpu_read(address);
}
void Bus::cpu_write(std::uint16_t address, std::uint8_t value) noexcept {
    if (address < 0x2000) ram_[address & 0x07ff] = value;
}
std::size_t Bus::nametable_index(std::uint16_t address) const noexcept {
    const auto offset = (address - 0x2000) & 0x0fff;
    const auto table = offset / 0x0400;
    const auto physical = cartridge_.mirroring() == Mirroring::vertical ? table % 2 : table / 2;
    return physical * 0x0400 + (offset & 0x03ff);
}
std::uint8_t Bus::ppu_read(std::uint16_t address) noexcept {
    address &= 0x3fff;
    if (address < 0x2000) return cartridge_.ppu_read(address);
    if (address < 0x3f00) return nametables_[nametable_index(address)];
    return 0;
}
void Bus::ppu_write(std::uint16_t address, std::uint8_t value) noexcept {
    address &= 0x3fff;
    if (address < 0x2000) cartridge_.ppu_write(address, value);
    else if (address < 0x3f00) nametables_[nametable_index(address)] = value;
}
} // namespace rse::nes
