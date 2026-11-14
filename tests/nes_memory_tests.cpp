// SPDX-License-Identifier: Apache-2.0
#include <rse/systems/nes/bus.hpp>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;
using rse::nes::Bus;
using rse::nes::Cartridge;
using rse::nes::Mirroring;
constexpr std::size_t header = 16;
constexpr std::size_t bank = 16384;
constexpr std::size_t chr_size = 8192;

void check(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}
template<class Exception, class Action>
void rejects(Action action, std::string_view reason) {
    try { action(); }
    catch (const Exception& error) {
        check(std::string_view(error.what()).find(reason) != std::string_view::npos,
              "error must identify the rejected feature or malformed data");
        return;
    }
    throw std::runtime_error("invalid input was accepted");
}

Bytes image(int prg_banks = 1, int chr_banks = 1, bool vertical = false) {
    Bytes bytes(header + prg_banks * bank + chr_banks * chr_size);
    bytes[0] = 'N'; bytes[1] = 'E'; bytes[2] = 'S'; bytes[3] = 0x1a;
    bytes[4] = static_cast<std::uint8_t>(prg_banks);
    bytes[5] = static_cast<std::uint8_t>(chr_banks);
    bytes[6] = vertical ? 1 : 0;
    // Every byte is synthetic; bank markers distinguish the two PRG halves.
    for (std::size_t i = header; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::uint8_t>((i * 17 + i / 251) & 0xff);
    bytes[header] = 0x11;
    bytes[header + bank - 1] = 0x22;
    if (prg_banks == 2) {
        bytes[header + bank] = 0x33;
        bytes[header + 2 * bank - 1] = 0x44;
    }
    return bytes;
}

void loader() {
    for (int prg : {1, 2}) for (int chr : {0, 1}) for (bool vertical : {false, true}) {
        auto bytes = image(prg, chr, vertical);
        const auto cartridge = Cartridge::parse(bytes);
        check(cartridge.prg_size() == prg * bank, "PRG size metadata");
        check(cartridge.chr_is_ram() == (chr == 0), "CHR type metadata");
        check(cartridge.mirroring() == (vertical ? Mirroring::vertical : Mirroring::horizontal),
              "mirroring metadata");
        const auto original = bytes[header];
        bytes[header] ^= 0xff;
        check(cartridge.cpu_read(0x8000) == original, "loader must own a copy");
        if (chr != 0) {
            const auto original_chr = bytes.back();
            bytes.back() ^= 0xff;
            check(cartridge.ppu_read(0x1fff) == original_chr, "loader must copy CHR ROM");
        }
        bytes.pop_back();
        rejects<std::invalid_argument>([&] { (void)Cartridge::parse(bytes); }, "truncated");
    }
    const auto full = image(2, 1);
    for (std::size_t length = 0; length < full.size(); ++length) {
        rejects<std::invalid_argument>([&] {
            (void)Cartridge::parse(std::span<const std::uint8_t>(full.data(), length));
        }, "truncated");
    }
    for (std::size_t offset = 0; offset < 4; ++offset) {
        auto bytes = image();
        bytes[offset] ^= 0xff;
        rejects<std::invalid_argument>([&] { (void)Cartridge::parse(bytes); }, "signature");
    }
    struct Invalid { std::size_t offset; std::uint8_t value; std::string_view reason; };
    const Invalid invalid[] = {
        {4, 0, "PRG"}, {4, 3, "PRG"}, {4, 255, "PRG"},
        {5, 2, "CHR"}, {5, 255, "CHR"},
        {6, 2, "battery"}, {6, 4, "trainers"}, {6, 8, "four-screen"},
        {6, 0x10, "mapper"}, {6, 0xf0, "mapper"},
        {7, 0x10, "mapper"}, {7, 0xf0, "mapper"},
        {7, 1, "console"}, {7, 2, "console"}, {7, 3, "console"},
        {7, 4, "variant"}, {7, 8, "NES 2.0"}, {7, 12, "variant"},
        {8, 1, "PRG RAM"}, {8, 255, "PRG RAM"}, {9, 1, "PAL"},
        {9, 2, "reserved"}, {10, 1, "extension"}, {10, 0x10, "extension"},
        {11, 1, "reserved"}, {12, 1, "reserved"}, {13, 1, "reserved"},
        {14, 1, "reserved"}, {15, 1, "reserved"}
    };
    for (const auto& test : invalid) {
        auto bytes = image();
        bytes[test.offset] = test.value;
        rejects<std::invalid_argument>([&] { (void)Cartridge::parse(bytes); }, test.reason);
    }
    for (int chr : {0, 1}) {
        auto bytes = image(2, chr);
        bytes.push_back(0);
        rejects<std::invalid_argument>([&] { (void)Cartridge::parse(bytes); }, "trailing");
    }
}

// A reserved directory avoids overwriting user files if the test is run by hand.
struct TestFiles {
    std::filesystem::path directory = "rse-nes-memory-test-files";
    std::filesystem::path rom = directory / "synthetic.nes";
    TestFiles() {
        check(std::filesystem::create_directory(directory), "test directory already exists");
    }
    ~TestFiles() {
        std::error_code ignored;
        std::filesystem::remove(rom, ignored);
        std::filesystem::remove(directory, ignored);
    }
    void write(const Bytes& bytes) const {
        std::ofstream output(rom, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        output.close();
        check(static_cast<bool>(output), "could not write synthetic test fixture");
    }
};

void files() {
    TestFiles files;
    rejects<std::runtime_error>([&] { (void)Cartridge::load_file(files.rom); }, "open");
    rejects<std::runtime_error>([&] { (void)Cartridge::load_file(files.directory); }, "cartridge file");
    const auto bytes = image(2, 1, true);
    files.write(bytes);
    const auto cartridge = Cartridge::load_file(files.rom);
    check(cartridge.prg_size() == 2 * bank && cartridge.mirroring() == Mirroring::vertical,
          "file metadata");
    check(cartridge.cpu_read(0xffff) == 0x44, "binary file payload");
    check(cartridge.ppu_read(0x1fff) == bytes.back(), "binary CHR payload");
    files.write(Bytes{'N', 'E'});
    rejects<std::invalid_argument>([&] { (void)Cartridge::load_file(files.rom); }, "truncated");
    auto oversized = bytes;
    oversized.resize(bytes.size() + 100000, 0);
    files.write(oversized);
    rejects<std::invalid_argument>([&] { (void)Cartridge::load_file(files.rom); }, "trailing");
    files.write(image(1, 0));
    check(Cartridge::load_file(files.rom).chr_is_ram(), "CHR RAM file variant");
}

void cpu_map() {
    for (int banks : {1, 2}) {
        const auto bytes = image(banks);
        Bus bus(Cartridge::parse(bytes));
        for (unsigned address = 0; address < 0x2000; ++address)
            check(bus.cpu_read(static_cast<std::uint16_t>(address)) == 0, "initial CPU RAM");
        // Writes through every alias must reach each of the other aliases.
        for (unsigned page : {0, 0x800, 0x1000, 0x1800}) {
            for (unsigned offset = 0; offset < 0x800; ++offset) {
                const auto value = static_cast<std::uint8_t>((offset * 13 + page / 0x800) & 0xff);
                bus.cpu_write(static_cast<std::uint16_t>(page + offset), value);
                for (unsigned alias : {0, 0x800, 0x1000, 0x1800})
                    check(bus.cpu_read(static_cast<std::uint16_t>(alias + offset)) == value,
                          "CPU RAM alias mismatch");
            }
        }
        for (unsigned address = 0x8000; address <= 0xffff; ++address) {
            const auto expected = bytes[header + (address - 0x8000) % (banks * bank)];
            check(bus.cpu_read(static_cast<std::uint16_t>(address)) == expected, "PRG read");
            bus.cpu_write(static_cast<std::uint16_t>(address), expected ^ 0xff);
            check(bus.cpu_read(static_cast<std::uint16_t>(address)) == expected, "PRG ROM changed");
        }
        check(bus.cpu_read(0x8000) == 0x11 && bus.cpu_read(0xbfff) == 0x22,
              "first PRG bank boundaries");
        check(bus.cpu_read(0xc000) == (banks == 1 ? 0x11 : 0x33) &&
              bus.cpu_read(0xffff) == (banks == 1 ? 0x22 : 0x44), "second PRG bank boundaries");
        const auto ram_end = bus.cpu_read(0x1fff);
        for (unsigned address = 0x2000; address < 0x8000; ++address) {
            bus.cpu_write(static_cast<std::uint16_t>(address), 0xab);
            check(bus.cpu_read(static_cast<std::uint16_t>(address)) == 0, "unconnected CPU device");
        }
        check(bus.cpu_read(0x1fff) == ram_end, "unmapped write changed CPU RAM");
    }
}

void ppu_map() {
    for (int chr : {0, 1}) {
        const auto bytes = image(1, chr);
        Bus bus(Cartridge::parse(bytes));
        for (unsigned address = 0; address < 0x2000; ++address) {
            const auto expected = chr ? bytes[header + bank + address] : 0;
            check(bus.ppu_read(static_cast<std::uint16_t>(address)) == expected, "initial CHR");
            const auto value = static_cast<std::uint8_t>(expected ^ 0xff);
            bus.ppu_write(static_cast<std::uint16_t>(address + 0x4000), value);
            check(bus.ppu_read(static_cast<std::uint16_t>(address)) == (chr ? expected : value),
                  "CHR ROM protection or CHR RAM write");
        }
    }
    for (bool vertical : {false, true}) {
        Bus bus(Cartridge::parse(image(1, 0, vertical)));
        for (unsigned address = 0x2000; address < 0x3f00; ++address)
            check(bus.ppu_read(static_cast<std::uint16_t>(address)) == 0, "initial nametable RAM");
        const std::array<unsigned, 4> horizontal_alias{1, 0, 3, 2};
        const std::array<unsigned, 4> vertical_alias{2, 3, 0, 1};
        const auto& aliases = vertical ? vertical_alias : horizontal_alias;
        for (unsigned table = 0; table < 4; ++table) {
            for (unsigned offset = 0; offset < 0x400; ++offset) {
                const auto value = static_cast<std::uint8_t>((offset + table * 29) & 0xff);
                const auto address = static_cast<std::uint16_t>(0x2000 + table * 0x400 + offset);
                const auto alias = static_cast<std::uint16_t>(0x2000 + aliases[table] * 0x400 + offset);
                bus.ppu_write(address, value);
                check(bus.ppu_read(alias) == value && bus.ppu_read(address) == value,
                      "nametable alias mismatch");
                const auto independent = static_cast<std::uint16_t>(
                    0x2000 + (vertical ? table ^ 1 : table ^ 2) * 0x400 + offset);
                bus.ppu_write(independent, value ^ 0xff);
                check(bus.ppu_read(address) == value, "independent nametable overlaps");
            }
        }
        for (unsigned address = 0x3000; address < 0x3f00; ++address) {
            const auto value = static_cast<std::uint8_t>((address * 7) & 0xff);
            bus.ppu_write(static_cast<std::uint16_t>(address), value);
            check(bus.ppu_read(static_cast<std::uint16_t>(address - 0x1000)) == value,
                  "$3000 nametable write mirror");
            bus.ppu_write(static_cast<std::uint16_t>(address - 0x1000), value ^ 0xff);
            check(bus.ppu_read(static_cast<std::uint16_t>(address)) == (value ^ 0xff),
                  "$3000 nametable read mirror");
        }
        bus.ppu_write(0xffff, 0xff);
        check(bus.ppu_read(0x3fff) == 0, "palette placeholder and wrapping");
        for (unsigned address = 0x3f00; address < 0x4000; ++address) {
            bus.ppu_write(static_cast<std::uint16_t>(address), 0xab);
            check(bus.ppu_read(static_cast<std::uint16_t>(address)) == 0, "palette placeholder");
        }
        bus.ppu_write(0x6000, 0x6b);
        check(bus.ppu_read(0xa000) == 0x6b && bus.ppu_read(0xe000) == 0x6b,
              "nametable address wrapping");
    }
}
} // namespace

int main() {
    try {
        loader();
        files();
        cpu_map();
        ppu_map();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
