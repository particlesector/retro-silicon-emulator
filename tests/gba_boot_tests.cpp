// SPDX-License-Identifier: Apache-2.0
#include <rse/session/builtins.hpp>
#include <rse/systems/gba/session.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
using namespace rse::gba;
using rse::session::load;
using Bytes = std::vector<std::uint8_t>;

void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

void put_word(Bytes& bytes, unsigned offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes.at(offset + i) = std::uint8_t(value >> (8 * i));
}

void memory_map() {
    Bytes bios(bios_size), rom(0x20008);
    put_word(bios, 0, 0xe1a00000);
    put_word(bios, 8, 0x11223344); // Pipeline-head word latched by the BIOS fetch.
    put_word(bios, 0x14, 0x55667788);
    put_word(rom, 0, 0x12345678);
    put_word(rom, 4, 0x89abcdef);
    put_word(rom, 0x1fffc, 0x76543210);
    put_word(rom, 0x20000, 0xfedcba98);
    auto bus = std::make_unique<Bus>(bios, rom);
    check(bus->fetch_arm(0) == 0xe1a00000, "read reset vector");
    check(bus->load_word(0x14) == 0x55667788, "BIOS data readable while executing inside BIOS");
    bus->store_word(0x14, 0);
    check(bus->load_half(0x16) == 0x5566, "BIOS is read-only");
    bus->fetch_arm(0x03000000);
    check(bus->load_word(0x14) == 0x11223344, "protected BIOS returns the latched instruction word");
    check(bus->load_half(2) == 0x1122 && bus->load_byte(1) == 0x33, "protected BIOS width selects the lane");

    auto tick = bus->ticks();
    bus->store_word(0x0203fffc, 0x12345678);
    check(bus->ticks() - tick == 6, "EWRAM word uses two 16-bit bus beats");
    bus->store_half(0x0207fffe, 0x9abc);
    bus->store_byte(0x020bfffd, 0x42);
    check(bus->load_word(0x0203fffc) == 0x9abc4278, "EWRAM mirrors and width lanes");
    tick = bus->ticks();
    bus->store_word(0x03007ffc, 0x87654321);
    check(bus->ticks() - tick == 1, "IWRAM word uses the internal 32-bit bus");
    check(bus->load_word(0x0300fffc) == 0x87654321, "IWRAM mirror");

    bus->store_byte(0x05000401, 0xab);
    check(bus->load_half(0x05000000) == 0xabab, "palette byte writes duplicate to both lanes");
    bus->store_word(0x06010000, 0xdeadbeef);
    bus->store_byte(0x06018001, 0x11);
    check(bus->load_word(0x06018000) == 0xdeadbeef, "OBJ VRAM ignores byte writes and aliases its second window");
    bus->store_byte(0x06000001, 0x42);
    check(bus->load_half(0x06020000) == 0x4242, "BG VRAM mirror and duplicated byte write");
    bus->store_half(0x07000000, 0x9876);
    bus->store_byte(0x07000400, 0x55);
    check(bus->load_half(0x07000400) == 0x9876, "OAM mirror ignores byte writes");

    auto access = [&](auto operation, std::uint32_t address, std::uint32_t value, unsigned duration) {
        const auto before = bus->ticks();
        check(((*bus).*operation)(address) == value, "Game Pak address, width or value");
        check(bus->ticks() - before == duration, "Game Pak elapsed ticks");
    };
    access(&Bus::fetch_arm, 0x08000000, 0x12345678, 8); // 5N + 3S.
    access(&Bus::fetch_arm, 0x08000004, 0x89abcdef, 6); // 3S + 3S.
    access(&Bus::fetch_thumb, 0x08000002, 0x1234, 5);
    access(&Bus::fetch_thumb, 0x08000004, 0xcdef, 3);
    bus->branch();
    access(&Bus::fetch_thumb, 0x08000006, 0x89ab, 5); // A branch can land on the next halfword.
    access(&Bus::load_byte, 0x08000003, 0x12, 5);
    access(&Bus::load_half, 0x08000004, 0xcdef, 3);
    access(&Bus::load_word, 0x081ffffc, 0xfffffffe, 8);
    access(&Bus::fetch_arm, 0x0801fffc, 0x76543210, 8);
    access(&Bus::fetch_arm, 0x08020000, 0xfedcba98, 8); // 128 KiB boundary restarts N.
    bus->store_word(0x08000000, 0);
    access(&Bus::load_word, 0x0a000000, 0x12345678, 10); // WS1: 5N + 5S.
    access(&Bus::load_word, 0x0c000000, 0x12345678, 14); // WS2: 5N + 9S.
    bus->store_half(0x04000204, 0x0018); // WS0: two waits N, one wait S.
    access(&Bus::load_word, 0x08000000, 0x12345678, 5);
    access(&Bus::load_word, 0x08000004, 0x89abcdef, 4);

    bus->reset();
    check(bus->ticks() == 0 && !bus->entry_fetch(), "cold reset clears bus clock and fetch record");
    check(bus->load_word(0x0203fffc) == 0 && bus->load_word(0x03007ffc) == 0, "cold reset clears both work RAMs");
}

void lcd_dma_irq() {
    auto bus = std::make_unique<Bus>(Bytes(bios_size), Bytes(cartridge_header_size));
    bus->store_word(0x02000000, 0x76543210);
    bus->store_word(0x02000004, 0xfedcba98);
    bus->store_word(0x040000d4, 0x02000000);
    bus->store_word(0x040000d8, 0x06000000);
    bus->store_word(0x040000dc, 0xd4000002); // DMA3, VBlank, word, IRQ, two units.
    bus->store_half(0x04000004, 0xa028); // LYC=160. VBlank and VCOUNT IRQ.
    bus->store_half(0x04000200, 0x0805);
    bus->store_byte(0x04000301, 0);
    check(bus->halted() && !bus->dma_pending(), "HALT and VBlank DMA both wait for their trigger");
    bus->idle_to_lcd_event();
    check(bus->ticks() == 1006 && (bus->load_half(0x04000004) & 7) == 2, "HBlank flag on first line");
    while (bus->ticks() < 197120) bus->idle_to_lcd_event();
    check(bus->ticks() == 197120 && !bus->halted() && bus->dma_pending() && !bus->irq(),
          "line 160 wakes HALT and requests DMA while IME masks the CPU IRQ");
    check(bus->load_word(0x04000004) == 0x00a0a02d, "VCOUNT and VBlank flags at line 160");
    check(bus->load_half(0x04000202) == 5, "LCD sources independently latch VBlank and VCOUNT IF bits");
    const auto dma_tick = bus->ticks();
    bus->step_dma();
    check(bus->ticks() == dma_tick + 10 && bus->dma_pending(), "DMA startup and first transfer");
    bus->step_dma();
    check(bus->ticks() == dma_tick + 18 && !bus->dma_pending(), "DMA second transfer and completion");
    check(bus->load_word(0x06000000) == 0x76543210 && bus->load_word(0x06000004) == 0xfedcba98,
          "DMA writes consecutive words into VRAM");
    check(bus->load_half(0x040000de) == 0x5400, "one-shot DMA clears its enable bit");
    check(bus->load_half(0x04000202) == 0x0805, "DMA completion requests IRQ");
    bus->store_byte(0x04000208, 1);
    check(bus->irq(), "IME exposes pending enabled requests");
    bus->store_byte(0x04000202, 5);
    check(bus->irq() && bus->load_half(0x04000202) == 0x0800, "IF low-byte write clears LCD IRQs only");
    bus->store_byte(0x04000203, 8);
    check(!bus->irq() && bus->load_half(0x04000202) == 0, "IF high-byte write clears DMA IRQ");

    bus->store_word(0x040000d8, 0x03000000);
    bus->store_word(0x040000dc, 0x85400002); // Immediate, word, fixed source and destination.
    check(bus->dma_pending(), "immediate DMA starts after the enable write");
    bus->step_dma();
    bus->step_dma();
    check(!bus->dma_pending() && bus->load_word(0x03000000) == 0x76543210, "immediate DMA uses fixed addresses");
}

void write_file(const std::filesystem::path& path, const Bytes& data) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    output.close();
    check(bool(output), "could not write boot fixture");
}

void authored_boot(const std::filesystem::path& prefix) {
    Bytes bios(bios_size), rom(cartridge_header_size);
    put_word(bios, 0, 0xea000006); // b 0x20
    put_word(bios, 8, 0xe1b0f00e); // movs pc, lr (SWI return)
    put_word(bios, 0x20, 0xe59f0004); // ldr r0, [pc, #4]
    put_word(bios, 0x24, 0xe12fff10); // bx r0
    put_word(bios, 0x2c, 0x08000000);
    put_word(rom, 0, 0xe3a0105a); // mov r1, #0x5a
    put_word(rom, 4, 0xef000000); // swi 0
    put_word(rom, 8, 0xeafffffe); // b .
    const EntryFetch expected{4, 0xe3a0105a, 4};
    auto machine = std::make_unique<Machine>(bios, rom);
    check(machine->cpu().pc == 0 && machine->bus().ticks() == 0, "boot begins at BIOS reset vector");
    for (int i = 0; i < 3; ++i) machine->step();
    check(machine->cpu().pc == 0x08000000 && !machine->bus().entry_fetch(), "branch alone does not fetch entry");
    machine->step();
    check(machine->bus().entry_fetch() == expected && machine->bus().ticks() == 12,
          "entry fetch starts after four BIOS accesses and takes eight ticks");
    check(machine->cpu().registers[1] == 0x5a, "first cartridge opcode executes");
    machine->step();
    check(machine->cpu().pc == 8, "cartridge SWI enters BIOS");
    machine->step();
    check(machine->cpu().pc == 0x08000008, "BIOS handler returns to cartridge");

    const auto path = [&](const char* suffix) {
        auto result = prefix;
        return result += suffix;
    };
    const auto bios_file = path("-bios.bin"), rom_file = path("-cart.gba"), short_file = path("-short.bin");
    struct Cleanup {
        std::array<std::filesystem::path, 3> files;
        ~Cleanup() {
            std::error_code error;
            for (const auto& file : files) std::filesystem::remove(file, error);
        }
    } cleanup{{bios_file, rom_file, short_file}};
    write_file(bios_file, bios);
    write_file(rom_file, rom);
    write_file(short_file, Bytes(191));
    auto active = load("gba", rom_file, bios_file);
    auto& gba = dynamic_cast<Session&>(*active);
    active->advance_to(4);
    check(!gba.entry_fetch(), "entry fetch is unavailable before its first tick elapses");
    active->advance_to(5);
    check(gba.entry_fetch() == expected && active->status().tick == 5, "session observes entry at the same tick");
    auto reject = [&](const auto& cartridge, const auto& firmware, const char* expected_error) {
        const auto* previous = active.get();
        try {
            active = load("gba", cartridge, firmware);
            throw std::logic_error("loader accepted invalid media");
        } catch (const std::exception& error) {
            check(std::string_view(error.what()).contains(expected_error), "loader diagnostic for invalid media");
        }
        check(active.get() == previous && active->status().tick == 5 && gba.entry_fetch() == expected,
              "failed load preserves the active session and its state");
    };
    reject(rom_file, std::filesystem::path{}, "GBA BIOS is required");
    reject(rom_file, path("-absent.bin"), "Cannot open GBA BIOS");
    reject(rom_file, short_file, "GBA BIOS must be exactly 16384 bytes");
    reject(short_file, bios_file, "Truncated GBA cartridge");
    active->advance_to(50);
    check(active->status().tick == 50, "preserved session can continue");
    active->reset();
    active->advance_to(50);
    check(gba.entry_fetch() == expected, "cold reset and chunking preserve the first fetch");
}

void private_boot(const std::filesystem::path& rom, const std::filesystem::path& bios) {
    const auto run = [&]() {
        auto active = load("gba", rom, bios);
        auto& gba = dynamic_cast<Session&>(*active);
        // One-tick requests stop before dispatch of the next instruction after entry.
        for (rse::Tick target = 1; target <= 6 * clock_hz; ++target) {
            gba.advance_to(target);
            if (const auto entry = gba.entry_fetch()) return *entry;
        }
        throw std::runtime_error("GBA cartridge entry was not fetched within six emulated seconds");
    };
    const auto first = run(), second = run();
    check(first == second, "separate cold boots disagree on the cartridge entry fetch");
    std::cout << "Two matching cold boots: fetch 0x08000000, tick " << first.tick
              << ", width " << first.width << " bytes, opcode 0x" << std::hex << first.opcode << '\n';
}
}

int main(int argc, char** argv) {
    try {
        if (argc < 2) throw std::invalid_argument("Specify memory, lcd-dma-irq, boot <prefix>, or private <rom> <bios>");
        const std::string_view test = argv[1];
        if (test == "memory" && argc == 2) memory_map();
        else if (test == "lcd-dma-irq" && argc == 2) lcd_dma_irq();
        else if (test == "boot" && argc == 3) authored_boot(argv[2]);
        else if (test == "private" && argc == 4) private_boot(argv[2], argv[3]);
        else throw std::invalid_argument("Invalid GBA boot test arguments");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
