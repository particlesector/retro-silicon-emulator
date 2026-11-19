// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <rse/core/state_codec.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace rse::nes {
enum class Mirroring { horizontal, vertical };

// Strict iNES mapper-0 subset. See docs/NES_MEMORY.md for flags and errors.
// Accepted bytes are copied; caller storage need not outlive the cartridge.
class Cartridge {
public:
    [[nodiscard]] static Cartridge parse(std::span<const std::uint8_t> image);
    [[nodiscard]] static Cartridge load_file(const std::filesystem::path& path);
    [[nodiscard]] Mirroring mirroring() const noexcept { return mirroring_; }
    [[nodiscard]] std::size_t prg_size() const noexcept { return prg_.size(); }
    [[nodiscard]] bool chr_is_ram() const noexcept { return chr_ram_; }
    // Out-of-range reads return zero; CHR writes outside $0000-$1FFF or to ROM
    // are ignored. PRG ROM has no writable interface.
    [[nodiscard]] std::uint8_t cpu_read(std::uint16_t address) const noexcept;
    [[nodiscard]] std::uint8_t ppu_read(std::uint16_t address) const noexcept;
    void ppu_write(std::uint16_t address, std::uint8_t value) noexcept;
    [[nodiscard]] std::uint64_t identity() const;
    void save_state(rse::state::Writer& out) const {if(chr_ram_) out(chr_);}
    void load_state(rse::state::Reader& in) {if(chr_ram_) in(chr_);}
private:
    Cartridge() = default;
    std::vector<std::uint8_t> prg_;
    std::array<std::uint8_t, 8192> chr_{};
    Mirroring mirroring_{Mirroring::horizontal};
    bool chr_ram_{};
};
} // namespace rse::nes
