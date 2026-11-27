// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace rse::cartridge {

// Sanyo LE26FV10N1TS, with immediate program and erase completion.
class Flash1M {
public:
    static constexpr unsigned bank_size = 0x10000;
    static constexpr unsigned image_size = 2 * bank_size;
    Flash1M();
    void reset() noexcept;
    [[nodiscard]] std::uint8_t read(std::uint16_t address) const noexcept;
    void write(std::uint16_t address, std::uint8_t value) noexcept;

    [[nodiscard]] std::span<const std::uint8_t> image() const noexcept { return bytes_; }
    void load_image(std::span<const std::uint8_t> image);
    [[nodiscard]] bool dirty() const noexcept { return dirty_; }
    void mark_clean() noexcept { dirty_ = false; }

private:
    enum class Command { idle, unlock_second, opcode, program, select_bank, erase_first, erase_second, erase_opcode };
    void erase(std::span<std::uint8_t> bytes) noexcept;
    std::array<std::uint8_t, image_size> bytes_;
    Command command_{Command::idle};
    unsigned bank_{};
    bool identify_{}, dirty_{};
};

}
