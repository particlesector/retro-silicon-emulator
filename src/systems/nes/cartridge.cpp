// SPDX-License-Identifier: Apache-2.0
#include <rse/systems/nes/cartridge.hpp>
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace rse::nes {
std::uint64_t Cartridge::identity() const {
    rse::state::Writer image;
    image(static_cast<std::uint32_t>(prg_.size()),mirroring_==Mirroring::vertical,chr_ram_);
    image.bytes.insert(image.bytes.end(),prg_.begin(),prg_.end());
    if(!chr_ram_) image(chr_);
    return rse::state::hash(image.bytes);
}
namespace {
constexpr std::size_t header_size = 16;
constexpr std::size_t prg_bank_size = 16384;
constexpr std::size_t chr_size = 8192;
constexpr std::size_t max_image_size = header_size + 2 * prg_bank_size + chr_size;
}

Cartridge Cartridge::parse(std::span<const std::uint8_t> image) {
    if (image.size() < header_size) throw std::invalid_argument("truncated iNES header");
    if (image[0] != 'N' || image[1] != 'E' || image[2] != 'S' || image[3] != 0x1a)
        throw std::invalid_argument("invalid iNES signature");
    if ((image[7] & 0x0c) == 0x08) throw std::invalid_argument("NES 2.0 is unsupported");
    if (image[7] & 0x0c) throw std::invalid_argument("unsupported iNES header variant");
    if (image[7] & 0x03) throw std::invalid_argument("unsupported console type");
    const auto mapper = (image[6] >> 4) | (image[7] & 0xf0);
    if (mapper != 0) throw std::invalid_argument("unsupported mapper: only mapper 0 is supported");
    if (image[6] & 0x04) throw std::invalid_argument("trainers are unsupported");
    if (image[6] & 0x02) throw std::invalid_argument("battery-backed memory is unsupported");
    if (image[6] & 0x08) throw std::invalid_argument("four-screen mirroring is unsupported");
    if (image[8] != 0) throw std::invalid_argument("explicit PRG RAM is unsupported");
    if (image[9] & 1) throw std::invalid_argument("PAL timing is unsupported");
    if (std::any_of(image.begin() + 9, image.begin() + 16,
                    [](auto byte) { return byte != 0; }))
        throw std::invalid_argument("unsupported iNES extension or nonzero reserved bytes");
    if (image[4] != 1 && image[4] != 2)
        throw std::invalid_argument("NROM requires 16 or 32 KiB PRG ROM");
    if (image[5] > 1) throw std::invalid_argument("NROM supports at most 8 KiB CHR ROM");
    const std::size_t prg_size = image[4] * prg_bank_size;
    const std::size_t expected = header_size + prg_size + image[5] * chr_size;
    if (image.size() < expected) throw std::invalid_argument("truncated PRG or CHR data");
    if (image.size() > expected) throw std::invalid_argument("unexpected trailing cartridge data");

    Cartridge cartridge;
    cartridge.mirroring_ = (image[6] & 1) ? Mirroring::vertical : Mirroring::horizontal;
    cartridge.chr_ram_ = image[5] == 0;
    cartridge.prg_.assign(image.begin() + header_size, image.begin() + header_size + prg_size);
    if (!cartridge.chr_ram_)
        std::copy_n(image.begin() + header_size + prg_size, chr_size, cartridge.chr_.begin());
    return cartridge;
}

Cartridge Cartridge::load_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open cartridge file");
    // One extra byte detects oversized input without allocating from file size.
    std::vector<std::uint8_t> image(max_image_size + 1);
    input.read(reinterpret_cast<char*>(image.data()), static_cast<std::streamsize>(image.size()));
    if (input.bad() || (input.fail() && !input.eof()))
        throw std::runtime_error("cannot read cartridge file");
    image.resize(static_cast<std::size_t>(input.gcount()));
    return parse(image);
}

std::uint8_t Cartridge::cpu_read(std::uint16_t address) const noexcept {
    if (address < 0x8000 || prg_.empty()) return 0;
    return prg_[(address - 0x8000) % prg_.size()];
}
std::uint8_t Cartridge::ppu_read(std::uint16_t address) const noexcept {
    return address < chr_size ? chr_[address] : 0;
}
void Cartridge::ppu_write(std::uint16_t address, std::uint8_t value) noexcept {
    if (chr_ram_ && address < chr_size) chr_[address] = value;
}
} // namespace rse::nes
