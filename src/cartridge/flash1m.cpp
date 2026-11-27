// SPDX-License-Identifier: Apache-2.0
#include <rse/cartridge/flash1m.hpp>
#include <algorithm>
#include <stdexcept>

namespace rse::cartridge {
namespace {
constexpr unsigned first = 0x5555, second = 0x2aaa, sector_size = 0x1000;
}

Flash1M::Flash1M() { bytes_.fill(0xff); }

void Flash1M::reset() noexcept {
    command_ = Command::idle;
    bank_ = 0;
    identify_ = false;
}

std::uint8_t Flash1M::read(std::uint16_t address) const noexcept {
    if (identify_ && address < 2) return address == 0 ? 0x62 : 0x13;
    return bytes_[bank_ * bank_size + address];
}

void Flash1M::erase(std::span<std::uint8_t> bytes) noexcept {
    if (std::ranges::all_of(bytes, [](auto value) { return value == 0xff; })) return;
    std::ranges::fill(bytes, 0xff);
    dirty_ = true;
}

void Flash1M::write(std::uint16_t address, std::uint8_t value) noexcept {
    const auto command = command_;
    command_ = Command::idle;
    switch (command) {
    case Command::idle:
        if (address == first && value == 0xaa) command_ = Command::unlock_second;
        break;
    case Command::unlock_second:
        if (address == second && value == 0x55) command_ = Command::opcode;
        break;
    case Command::opcode:
        if (address != first) break;
        switch (value) {
        case 0x90: identify_ = true; break;
        case 0xf0: identify_ = false; break;
        case 0xa0: if (!identify_) command_ = Command::program; break;
        case 0xb0: if (!identify_) command_ = Command::select_bank; break;
        case 0x80: if (!identify_) command_ = Command::erase_first; break;
        }
        break;
    case Command::program: {
        auto& byte = bytes_[bank_ * bank_size + address];
        const auto programmed = std::uint8_t(byte & value);
        dirty_ |= programmed != byte;
        byte = programmed;
        break;
    }
    case Command::select_bank:
        if (address == 0 && value < 2) bank_ = value;
        break;
    case Command::erase_first:
        if (address == first && value == 0xaa) command_ = Command::erase_second;
        break;
    case Command::erase_second:
        if (address == second && value == 0x55) command_ = Command::erase_opcode;
        break;
    case Command::erase_opcode:
        if (address == first && value == 0x10) erase(bytes_);
        else if (address % sector_size == 0 && value == 0x30)
            erase(std::span(bytes_).subspan(bank_ * bank_size + address, sector_size));
        break;
    }
}

void Flash1M::load_image(std::span<const std::uint8_t> image) {
    if (image.size() != image_size) throw std::invalid_argument("Flash image must be exactly 131072 bytes");
    std::ranges::copy(image, bytes_.begin());
    reset();
    mark_clean();
}

}
