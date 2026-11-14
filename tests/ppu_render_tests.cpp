// SPDX-License-Identifier: Apache-2.0
#include "ppu_test_bus.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>

namespace {
using rse::video::Rp2c02;
using rse::video::PpuFrame;
struct Object { unsigned y, tile, attributes, x; };
enum class Split { none, control, scroll, mask };
struct Scene {
    const char* name;
    unsigned x{}, y{}, control{}, mask{0x0a};
    Split split{Split::none};
    std::vector<Object> objects;
};
std::vector<Scene> scenes() {
    return {
        {"background"},
        {"scroll-wrap",253,237,3},
        {"grayscale-emphasis",5,7,0x10,0xab},
        {"sprites-8",0,0,0,0x1e,Split::none,
            {{20,2,0x20,24},{20,3,1,24},{40,4,0x42,50},{80,5,0x83,100},{120,6,0,255}}},
        {"sprites-16",0,0,0x28,0x1e,Split::none,
            {{20,3,0xc1,10},{40,4,2,90},{60,5,0x80,150}}},
        {"control-split",0,0,0,0x0a,Split::control},
        {"fine-x-split",0,0,0,0x0a,Split::scroll},
        {"mask-split",0,0,0,0x0a,Split::mask}
    };
}
unsigned tile(unsigned nt, unsigned x, unsigned y) { return (x * 3 + y * 5 + nt * 7) & 255; }
unsigned pattern(unsigned t, unsigned row, unsigned column, unsigned table) { return (t + row + column + table) & 3; }
unsigned color(unsigned index) {
    if ((index & 0x13) == 0x10) index &= 15;
    return (index * 3 + 5) & 63;
}
void configure(PpuTestBus& bus, Rp2c02& ppu, const Scene& scene) {
    bus.recording = false;
    for (unsigned table = 0; table < 2; ++table) for (unsigned t = 0; t < 256; ++t)
        for (unsigned row = 0; row < 8; ++row) {
            unsigned low = 0, high = 0;
            for (unsigned column = 0; column < 8; ++column) {
                const auto value = pattern(t, row, column, table);
                low |= (value & 1) << (7 - column);
                high |= ((value >> 1) & 1) << (7 - column);
            }
            bus.ram[table * 0x1000 + t * 16 + row] = static_cast<std::uint8_t>(low);
            bus.ram[table * 0x1000 + t * 16 + row + 8] = static_cast<std::uint8_t>(high);
        }
    for (unsigned nt = 0; nt < 4; ++nt) {
        const auto base = 0x2000 + nt * 0x400;
        for (unsigned y = 0; y < 30; ++y) for (unsigned x = 0; x < 32; ++x)
            bus.ram[base + y * 32 + x] = static_cast<std::uint8_t>(tile(nt,x,y));
        for (unsigned ay = 0; ay < 8; ++ay) for (unsigned ax = 0; ax < 8; ++ax) {
            unsigned attribute = 0;
            for (unsigned qy = 0; qy < 2; ++qy) for (unsigned qx = 0; qx < 2; ++qx) {
                const auto palette = (nt + ax * 2 + qx + 2 * (ay * 2 + qy)) & 3;
                attribute |= palette << (qy * 4 + qx * 2);
            }
            bus.ram[base + 0x3c0 + ay * 8 + ax] = static_cast<std::uint8_t>(attribute);
        }
    }
    for (unsigned index = 0; index < 32; ++index) memory(ppu, 0x3f00 + index, static_cast<std::uint8_t>(color(index)));
    std::array<std::uint8_t,256> objects;
    objects.fill(0xff);
    for (std::size_t i = 0; i < scene.objects.size(); ++i) {
        const auto& object = scene.objects[i];
        objects[i*4] = static_cast<std::uint8_t>(object.y);
        objects[i*4+1] = static_cast<std::uint8_t>(object.tile);
        objects[i*4+2] = static_cast<std::uint8_t>(object.attributes);
        objects[i*4+3] = static_cast<std::uint8_t>(object.x);
    }
    oam(ppu, objects);
    address(ppu, 0);
    ppu.cpu_write(0, static_cast<std::uint8_t>(scene.control));
    ppu.cpu_write(5, static_cast<std::uint8_t>(scene.x));
    ppu.cpu_write(5, static_cast<std::uint8_t>(scene.y));
    ppu.cpu_write(1, static_cast<std::uint8_t>(scene.mask));
}

// Independent spatial oracle: resolve the world coordinate directly. It has no
// shift registers, prefetch state, dot dispatcher, or PPU register access.
PpuFrame oracle(const Scene& scene) {
    PpuFrame expected;
    for (unsigned y = 0; y < 240; ++y) for (unsigned x = 0; x < 256; ++x) {
        unsigned mask = scene.mask;
        unsigned fine_adjust = 0;
        unsigned table = (scene.control >> 4) & 1;
        if (scene.split == Split::control && y >= 101) table = 1;
        if (scene.split == Split::scroll && (y > 80 || (y == 80 && x >= 128))) fine_adjust = 3;
        if (scene.split == Split::mask && (y > 120 || (y == 120 && x >= 128))) mask = 0x6b;
        const unsigned wx = (x + scene.x + ((scene.control & 1) ? 256 : 0) + fine_adjust) % 512;
        const unsigned wy = (y + scene.y + ((scene.control & 2) ? 240 : 0)) % 480;
        const unsigned nt = (wy / 240) * 2 + wx / 256;
        const unsigned tx = (wx % 256) / 8, ty = (wy % 240) / 8;
        unsigned bg = pattern(tile(nt,tx,ty), wy % 8, wx % 8, table);
        if (!(mask & 8) || (x < 8 && !(mask & 2))) bg = 0;
        const unsigned bg_palette = (nt + tx / 2 + 2 * (ty / 2)) & 3;
        unsigned palette_index = bg ? bg_palette * 4 + bg : 0;
        unsigned eligible = 0;
        const unsigned height = (scene.control & 0x20) ? 16 : 8;
        for (const auto& object : scene.objects) {
            if (y == 0 || y <= object.y || y > object.y + height) continue;
            if (++eligible > 8) break;
            if (!(mask & 0x10) || (x < 8 && !(mask & 4)) || x < object.x || x >= object.x + 8) continue;
            unsigned row = y - object.y - 1, column = x - object.x;
            if (object.attributes & 0x80) row = height - 1 - row;
            if (object.attributes & 0x40) column = 7 - column;
            const unsigned sprite_table = height == 16 ? object.tile & 1 : (scene.control >> 3) & 1;
            const unsigned sprite_tile = height == 16 ? (object.tile & 0xfe) + row / 8 : object.tile;
            const unsigned value = pattern(sprite_tile, row % 8, column, sprite_table);
            if (!value) continue;
            if (!bg || !(object.attributes & 0x20)) palette_index = 0x10 + (object.attributes & 3) * 4 + value;
            break; // An earlier behind-background sprite still blocks later ones.
        }
        unsigned sample = color(palette_index);
        if (mask & 1) sample &= 0x30;
        expected.samples[y*256+x] = static_cast<std::uint16_t>(sample | ((mask & 0xe0) << 1));
    }
    return expected;
}

// Generated only by --print-goldens, which calls the spatial oracle, never Rp2c02.
constexpr std::array<std::uint32_t,8> golden_hashes{
    0x23ac8dc5u, 0x7a4b8f51u, 0x7b6b09f5u, 0xf4f5ed3au,
    0x139daa35u, 0x3caafe45u, 0x9d9b89c5u, 0x344d9805u
};
void frame_cases() {
    const auto fixtures = scenes();
    for (std::size_t i = 0; i < fixtures.size(); ++i) {
        const auto& scene = fixtures[i];
        const auto expected = oracle(scene);
        check(frame_hash(expected) == golden_hashes[i], std::string(scene.name) + ": oracle/golden mismatch");
        PpuTestBus bus;
        Rp2c02 ppu(bus);
        for (unsigned replay = 0; replay < 2; ++replay) {
            if (replay) ppu.reset();
            configure(bus, ppu, scene);
            while (ppu.frame().number == 0) {
                const auto state = ppu.state();
                if (scene.split == Split::control && state.scanline == 100 && state.dot == 257) ppu.cpu_write(0, 0x10);
                if (scene.split == Split::scroll && state.scanline == 80 && state.dot == 129) {
                    ppu.cpu_write(5, 3); ppu.cpu_write(5, 0);
                }
                if (scene.split == Split::mask && state.scanline == 120 && state.dot == 129) ppu.cpu_write(1, 0x6b);
                ppu.clock();
                check(ppu.state().dots < 100000, "frame publication timed out");
            }
            for (std::size_t pixel = 0; pixel < expected.samples.size(); ++pixel)
                check(ppu.frame().samples[pixel] == expected.samples[pixel], std::string(scene.name) + ": pixel (" +
                    std::to_string(pixel % 256) + "," + std::to_string(pixel / 256) + ") expected " +
                    std::to_string(expected.samples[pixel]) + " got " + std::to_string(ppu.frame().samples[pixel]));
            check(frame_hash(ppu.frame()) == golden_hashes[i], std::string(scene.name) + ": completed frame hash mismatch");
            const auto retained = ppu.frame().samples;
            clocks(ppu, 12000); // Includes drawing into the other frame buffer.
            check(ppu.frame().samples == retained && ppu.frame().number == 1, "published frame changed before next publication");
        }
    }
}

void solid(PpuTestBus& bus, Rp2c02& ppu, unsigned sprite_x, unsigned attributes = 0) {
    bus.recording = false;
    for (unsigned row = 0; row < 8; ++row) { bus.ram[row] = 0xff; bus.ram[16+row] = 0xff; }
    std::array<std::uint8_t,256> objects;
    objects.fill(0xff);
    objects[0] = 0; objects[1] = 1; objects[2] = static_cast<std::uint8_t>(attributes);
    objects[3] = static_cast<std::uint8_t>(sprite_x);
    oam(ppu, objects);
    memory(ppu, 0x3f00, 5); memory(ppu, 0x3f01, 11); memory(ppu, 0x3f11, 22);
    address(ppu, 0); ppu.cpu_write(5, 0); ppu.cpu_write(5, 0); ppu.cpu_write(1, 0x1e);
}
void sprite_flags() {
    for (unsigned x : {0, 16, 255}) for (bool behind : {false, true}) {
        PpuTestBus bus;
        Rp2c02 ppu(bus);
        solid(bus, ppu, x, behind ? 0x20 : 0);
        until(ppu, 1, x+1);
        check(!(ppu.state().status & 0x40), "sprite-zero hit before overlapping pixel");
        ppu.clock();
        check(bool(ppu.state().status & 0x40) == (x != 255), "sprite-zero hit priority/right-edge exclusion");
        until(ppu, 241, 2);
        check(ppu.frame().samples[256+x] == (behind ? 11 : 22), "sprite foreground/background priority");
        check(std::all_of(ppu.frame().samples.begin(), ppu.frame().samples.begin()+256,
                          [](auto sample) { return sample == 11; }), "pre-render must not select sprites for line 0");
        if (x != 255) {
            check(ppu.state().status & 0x40, "sprite-zero hit should persist across vblank");
            until(ppu, 261, 1); ppu.clock();
            check(!(ppu.state().status & 0x60), "pre-render must clear sprite flags");
        }
    }
    for (unsigned mask : {0x18, 0x1a, 0x1c, 0x0a, 0x14}) {
        PpuTestBus bus;
        Rp2c02 ppu(bus);
        solid(bus, ppu, 0);
        ppu.cpu_write(1, static_cast<std::uint8_t>(mask));
        until(ppu, 2, 0);
        check(!(ppu.state().status & 0x40), "disabled/clipped layer must prevent sprite-zero hit");
    }
    // Earlier behind-background sprite blocks an overlapping later foreground sprite.
    PpuTestBus bus;
    Rp2c02 ppu(bus);
    solid(bus, ppu, 16, 0x20);
    ppu.cpu_write(3, 4);
    for (auto value : {0,1,0,16}) ppu.cpu_write(4, static_cast<std::uint8_t>(value));
    until(ppu, 241, 2);
    check(ppu.frame().samples[256+16] == 11, "sprite priority must resolve before background priority");
}
void overflow_cases() {
    for (unsigned kind = 0; kind < 4; ++kind) {
        PpuTestBus bus; bus.recording = false;
        Rp2c02 ppu(bus);
        std::array<std::uint8_t,256> objects;
        objects.fill(0xff);
        for (unsigned i = 0; i < 8; ++i) objects[i*4] = 0;
        if (kind == 1) objects[8*4] = 0; // Actual ninth sprite.
        if (kind == 2) objects[9*4+1] = 0; // False positive: tile byte on diagonal.
        if (kind == 3) objects[9*4] = 0; // False negative: ninth Y skipped by diagonal.
        oam(ppu, objects); ppu.cpu_write(1, 8);
        until(ppu, 0, 130);
        check(!(ppu.state().status & 0x20), "overflow before ninth evaluation");
        check(ppu.cpu_read(4) == (kind == 1 ? 0 : 0xff), "odd evaluation dot must expose primary OAM");
        ppu.clock();
        check(ppu.cpu_read(4) == 0, "full secondary OAM must drive even evaluation reads");
        if (kind == 1) check(ppu.state().status & 0x20, "ninth sprite overflow timing");
        until(ppu, 0, 257);
        check(bool(ppu.state().status & 0x20) == (kind == 1 || kind == 2), "diagonal sprite overflow behavior");
    }
}
void sprite_fetches() {
    PpuTestBus bus;
    Rp2c02 ppu(bus);
    std::array<std::uint8_t,256> objects;
    objects.fill(0xff);
    objects[0] = 0; objects[1] = 3; objects[2] = 0xc0; objects[3] = 0;
    oam(ppu, objects);
    bus.ram[0x1037] = 1; bus.ram[0x103f] = 0;
    memory(ppu, 0x3f00, 5); memory(ppu, 0x3f11, 22);
    address(ppu, 0); ppu.cpu_write(0, 0x20); ppu.cpu_write(1, 0x14);
    until(ppu, 0, 257); bus.trace.clear();
    clocks(ppu, 8);
    check(bus.trace == std::vector<PpuAccess>{{0x2000,0,false,600}, {0x2000,0,false,602},
                                             {0x1037,1,false,604}, {0x103f,0,false,606}},
          "8x16 vertical flip/table select fetch address and timestamp");
    until(ppu, 241, 2);
    check(ppu.frame().samples[256] == 22 && ppu.frame().samples[257] == 5,
          "sprite horizontal flip/bit ordering");
}
void forced_blank() {
    PpuTestBus bus; bus.recording = false;
    Rp2c02 ppu(bus);
    memory(ppu, 0x3f00, 5); memory(ppu, 0x3f04, 0x2f);
    address(ppu, 0x3f04); ppu.cpu_write(1, 0xa1);
    until(ppu, 241, 2);
    check(std::all_of(ppu.frame().samples.begin(), ppu.frame().samples.end(),
                     [](auto pixel) { return pixel == 0x160; }), "forced-blank palette override/grayscale/emphasis");
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--print-goldens") {
            for (const auto& scene : scenes()) std::cout << scene.name << " 0x" << std::hex << frame_hash(oracle(scene)) << '\n';
            return 0;
        }
        frame_cases(); sprite_flags(); overflow_cases(); sprite_fetches(); forced_blank();
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
