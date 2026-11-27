// SPDX-License-Identifier: Apache-2.0
#include <rse/session/builtins.hpp>
#include <rse/systems/gba/machine.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
namespace fs = std::filesystem;
using namespace rse::gba;
using Bytes = std::vector<std::uint8_t>;
constexpr unsigned flash = 0x0e000000;
constexpr auto size = rse::cartridge::Flash1M::image_size;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

Bytes cartridge() {
    Bytes rom(cartridge_header_size + 16);
    std::ranges::copy(std::string_view("FLASH1M_V103"), rom.begin() + cartridge_header_size);
    return rom;
}

void commands() {
    auto bus = std::make_unique<Bus>(Bytes(bios_size), cartridge());
    const auto write = [&](unsigned address, std::uint8_t value) { bus->store_byte(flash + address, value); };
    const auto read = [&](unsigned address) { return bus->load_byte(flash + address); };
    const auto unlock = [&] { write(0x5555, 0xaa); write(0x2aaa, 0x55); };
    const auto command = [&](std::uint8_t value) { unlock(); write(0x5555, value); };
    const auto program = [&](unsigned address, std::uint8_t value) { command(0xa0); write(address, value); };
    const auto bank = [&](std::uint8_t number) { command(0xb0); write(0, number); };

    check(read(0) == 0xff && bus->ticks() == 5, "erased byte and default backup wait");
    bus->store_half(0x04000204, 3);
    const auto tick = bus->ticks();
    check(read(1) == 0xff && bus->ticks() == tick + 9, "WAITCNT backup wait");
    write(0x5555, 0x90);
    check(read(0) == 0xff, "bare opcode ignored");
    command(0x90);
    check(read(0) == 0x62 && read(1) == 0x13, "Sanyo manufacturer and device ID");
    command(0xf0);
    check(read(0) == 0xff && read(1) == 0xff && !bus->flash()->dirty(), "exit ID without altering data");
    program(0, 0xf5);
    program(0, 0x9f);
    check(read(0) == 0x95, "program only clears bits");
    program(0x1000, 0x12);
    program(0xffff, 0x34);
    bank(1);
    check(read(0) == 0xff && read(0x1000) == 0xff && read(0xffff) == 0xff, "bank one starts erased");
    program(0, 0xf0);
    program(0x0fff, 0x44);
    program(0x1000, 0x55);
    program(0x1fff, 0x66);
    program(0x2000, 0x77);
    program(0xffff, 0x88);
    bank(2);
    check(read(0) == 0xf0, "invalid bank does not change selection");
    command(0x80); unlock(); write(0x1001, 0x30);
    check(read(0x1000) == 0x55, "unaligned sector command ignored");
    command(0x80); unlock(); write(0x1000, 0x30);
    check(read(0x1000) == 0xff && read(0x1fff) == 0xff, "sector endpoints erased");
    check(read(0x0fff) == 0x44 && read(0x2000) == 0x77, "adjacent sectors preserved");
    command(0x90);
    unlock();
    bus->reset();
    write(0x5555, 0xa0);
    write(0, 0);
    check(read(0) == 0x95 && read(0x1000) == 0x12 && read(0xffff) == 0x34,
          "reset clears decoder, ID mode and bank selection, preserving bank zero");
    bank(1);
    check(read(0) == 0xf0 && read(0xffff) == 0x88, "reset preserves bank one");
    command(0x80); command(0x10);
    check(std::ranges::all_of(bus->flash()->image(), [](auto byte) { return byte == 0xff; }),
          "chip erase clears both banks");

    auto machine = std::make_unique<Machine>(Bytes(bios_size), cartridge());
    Bytes seed(size, 0x35);
    machine->flash()->load_image(seed);
    machine->reset();
    check(std::ranges::equal(machine->bus().flash()->image(), seed), "machine reset preserves cartridge contents");
    auto plain = std::make_unique<Bus>(Bytes(bios_size), Bytes(cartridge_header_size));
    check(!plain->flash(), "cartridge without FLASH1M marker has no flash");
}

void write_file(const fs::path& path, const Bytes& bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.close();
    check(bool(output), "write fixture file");
}

Bytes read_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    check(bool(input), "open fixture save");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

Bytes flash_program_bios() {
    // Load from the selected flash byte, shift it right, then program it.
    // Cold loads thus distinguish a fresh device (FF) and an existing save.
    constexpr std::uint32_t program[]{
        0xe59f002c, 0xe59f102c, 0xe59f202c, // LDR r0/r1/r2, literals
        0xe5d23000, 0xe1a030a3,             // LDRB r3,[r2]; LSR r3,#1
        0xe3a040aa, 0xe5c04000,             // STRB AA,[r0]
        0xe3a04055, 0xe5c14000,             // STRB 55,[r1]
        0xe3a040a0, 0xe5c04000,             // STRB A0,[r0]
        0xe5c23000, 0xeafffffe,             // STRB r3,[r2]; loop
        flash + 0x5555, flash + 0x2aaa, flash + 0x1234
    };
    Bytes bios(bios_size);
    for (unsigned i = 0; i < std::size(program); ++i)
        for (unsigned j = 0; j < 4; ++j) bios[i * 4 + j] = std::uint8_t(program[i] >> (j * 8));
    return bios;
}

void files(const fs::path& dir) {
    fs::create_directories(dir);
    const auto bios = dir / "bios.bin";
    const auto rom = dir / "game.gba";
    const auto other = dir / "other.gba";
    const auto save = dir / "game.gba.sav";
    const auto other_save = dir / "other.gba.sav";
    for (const auto& path : {save, other_save}) fs::remove(path);
    write_file(bios, flash_program_bios());
    write_file(rom, cartridge());
    write_file(other, cartridge());
    {
        auto game = rse::session::load("gba", rom, bios);
        game->flush_save();
        check(!fs::exists(save), "clean flash does not create a sidecar");
        game->advance_to(100);
        game->reset();
        game->flush_save();
    }
    Bytes expected(size, 0xff);
    expected[0x1234] = 0x7f;
    check(read_file(save) == expected, "first flush writes raw 128 KiB after reset");
    auto independent = rse::session::load("gba", other, bios);
    independent->advance_to(100);
    independent->flush_save();
    check(read_file(other_save) == expected, "distinct cartridge paths have independent saves");

    auto game = rse::session::load("gba", rom, bios);
    game->advance_to(100);
#ifdef _WIN32
    // Allow reads of the last successful file while denying replacement.
    const auto handle = CreateFileW(save.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    check(handle != INVALID_HANDLE_VALUE, "lock existing save against replacement");
#else
    // POSIX permits rename over open files. A directory blocks replacement.
    const auto previous = dir / "previous.sav";
    fs::rename(save, previous);
    fs::create_directory(save);
#endif
    bool failed = false;
    try { game->flush_save(); }
    catch (const std::runtime_error& error) { failed = std::string_view(error.what()).contains("Cannot flush GBA save"); }
#ifdef _WIN32
    CloseHandle(handle);
#else
    check(fs::is_directory(save), "failed replacement preserves the destination directory");
    fs::remove(save);
    fs::rename(previous, save);
#endif
    check(failed && read_file(save) == expected, "failed replacement reports error and preserves the previous save");
    game->flush_save();
    expected[0x1234] = 0x3f;
    check(read_file(save) == expected, "retry replaces the save with reloaded and updated data");
    check(read_file(other_save)[0x1234] == 0x7f, "replacement preserves the other cartridge");
    for (const auto& item : fs::directory_iterator(dir))
        check(!item.path().filename().string().starts_with(".rse-save-"), "staging files cleaned up");

    for (const auto& invalid : {Bytes{}, Bytes(size - 1, 0x55), Bytes(size + 1, 0xab)}) {
        write_file(save, invalid);
        bool rejected = false;
        try { (void)rse::session::load("gba", rom, bios); }
        catch (const std::runtime_error& error) { rejected = std::string_view(error.what()).contains("exactly 131072 bytes"); }
        check(rejected && read_file(save) == invalid, "malformed sidecar rejected and preserved");
    }
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "commands") commands();
        else if (argc == 3 && std::string_view(argv[1]) == "files") files(argv[2]);
        else throw std::invalid_argument("Expected commands or files <fixture dir>");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
