// SPDX-License-Identifier: Apache-2.0
#include <rse/session/builtins.hpp>
#include <rse/systems/gba/bus.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

namespace {
using namespace rse::gba;
using rse::Tick;
using Bytes = std::vector<std::uint8_t>;
constexpr std::uint32_t io = 0x04000000, ram = 0x03000000;

void check(bool ok, const char* reason) {
    if (!ok) throw std::runtime_error(reason);
}

struct Program {
    Bytes bytes;
    unsigned pc{}, literals{0x300};
    explicit Program(unsigned size) : bytes(size) {}
    void word(unsigned address, std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) bytes.at(address + i) = std::uint8_t(value >> (8 * i));
    }
    void emit(std::uint32_t opcode) { word(pc, opcode); pc += 4; }
    void load(unsigned destination, std::uint32_t value) {
        emit(0xe59f0000 | (destination << 12) | (literals - pc - 8)); // LDR rd,[pc,#offset].
        word(literals, value);
        literals += 4;
    }
    void store(std::uint32_t address, std::uint32_t value, bool halfword = false) {
        load(0, address);
        load(1, value);
        emit(halfword ? 0xe1c010b0 : 0xe5801000); // STRH/STR r1,[r0].
    }
};

struct Fixture {
    Program bios{bios_size}, cart{0x400};
    Fixture() {
        bios.store(io + 0x84, 0x80, true);
        bios.store(io + 0x80, 0x5204f177); // PSG four to left, pulse 1 to right. A left, B right.
        bios.store(io + 0x62, 0xf080, true);
        bios.store(io + 0x64, 0x8600, true);
        bios.store(io + 0x68, 0x9080, true);
        bios.store(io + 0x6c, 0x8700, true);
        bios.store(io + 0x70, 0x0040, true); // CPU writes bank 0 while bank 1 is selected.
        for (unsigned i = 0; i < 16; i += 4) bios.store(io + 0x90 + i, 0xeeca9631);
        bios.store(io + 0x70, 0x80, true);
        bios.store(io + 0x72, 0x2000, true);
        bios.store(io + 0x74, 0x8400, true);
        bios.store(io + 0x78, 0x5000, true);
        bios.store(io + 0x7c, 0x8032, true);
        bios.store(ram, 0x3030d0d0);
        bios.store(ram + 4, 0x10f010f0);
        bios.store(io + 0xa0, 0x3030d0d0);
        bios.store(io + 0xa4, 0x10f010f0);
        bios.store(io + 0xbc, ram);
        bios.store(io + 0xc0, io + 0xa0);
        bios.store(io + 0xc4, 0xb3000001); // FIFO mode overrides 16-bit width, count and destination increment.
        bios.store(io + 0xc8, ram + 4);
        bios.store(io + 0xcc, io + 0xa4);
        bios.store(io + 0xd0, 0xb3000001);
        bios.store(io + 0x100, 0x0080fe00);
        bios.store(io + 0x104, 0x0080ff00);
        bios.store(io + 0x20, 0x100, true);
        bios.store(io + 0x26, 0x100, true);
        bios.store(0x05000000, 0x4210, true);
        bios.store(io, 0x0405, true); // Mode 5 leaves a visible backdrop inside the 240×160 frame.
        bios.load(0, 0x08000000);
        bios.emit(0xe12fff10);

        // Cartridge reads active-low KEYINPUT into the first bitmap pixel.
        cart.emit(0xea00002e); // Branch over the cartridge header.
        cart.pc = 0xc0;
        cart.load(0, io + 0x130);
        cart.load(1, 0x06000000);
        cart.emit(0xe1d020b0);
        cart.emit(0xe1c120b0);
        cart.emit(0xeafffffc);
    }
};

void psg_channels() {
    using Stereo = std::array<std::int16_t, 2>;
    for (unsigned channel = 0; channel < 4; ++channel) {
        rse::audio::GbaApu apu;
        auto write = [&](unsigned offset, std::uint16_t value) { apu.write(offset, value, 0xffff, 0); };
        write(0x24, 0x80);
        write(0x20, (1u << (12 + channel)) | 0x70); // Only the selected generator reaches left.
        write(0x22, 2);
        if (channel == 2) {
            write(0x10, 0x40);
            for (unsigned i = 0; i < 16; i += 2) write(0x30 + i, 0xffff);
            write(0x10, 0x80);
            write(0x12, 0x2000);
        } else {
            constexpr unsigned offsets[]{0x02, 0x08, 0, 0x18};
            write(offsets[channel], 0xf03f);
        }
        constexpr unsigned triggers[]{0x04, 0x0c, 0x14, 0x1c};
        write(triggers[channel], channel == 0 ? 0xc000 : 0x8000);
        const auto stereo = apu.sample(512);
        check(stereo[1] == 0 && std::abs(int(stereo[0])) == (channel == 2 ? 3584 : 7680),
              "isolated PSG generator level and pan");
        if (channel == 0) check(apu.sample(32768) == Stereo{}, "length-one pulse stops on the first length clock");
        apu.write(0x24, 0, 0xffff, 33000);
        check(apu.sample(33280) == Stereo{}, "master disable mutes PSG");
    }
}

void fifo_timing() {
    auto bus = std::make_unique<Bus>(Bytes(bios_size), Bytes(cartridge_header_size), true);
    std::vector<TimingEvent> trace;
    trace.reserve(12);
    bus->set_timing_sink([](void* context, TimingEvent event) noexcept {
        if (event.kind == TimingKind::dma_start || event.kind == TimingKind::dma_complete ||
            event.kind == TimingKind::sound_fifo_request)
            static_cast<std::vector<TimingEvent>*>(context)->push_back(event);
    }, &trace);
    const auto run = [&](Tick limit) {
        while (bus->ticks() < limit) {
            if (bus->dma_pending()) bus->step_dma(limit);
            else bus->idle(limit);
        }
    };
    bus->store_half(io + 0x84, 0x80);
    bus->store_half(io + 0x82, 0x5204); // A full left on timer 0, B half right on timer 1.
    for (int i = 0; i < 4; ++i) bus->store_word(io + 0xa0, 0x40404040);
    bus->store_byte(io + 0xa0, 0x40); // Refill when the seventeenth queued byte becomes sixteen.
    bus->store_byte(io + 0xa4, 0xc0);
    bus->store_word(ram, 0x20202020);
    bus->store_word(io + 0xbc, ram);
    bus->store_word(io + 0xc0, io + 0xa0);
    bus->store_word(io + 0xc4, 0xb3000001);
    run(99);
    bus->store_word(io + 0x100, 0x0080fe00); // Starts at 100 and first overflows at 612.
    run(109);
    bus->store_word(io + 0x104, 0x0080fe00); // Starts at 110 and first overflows at 622.
    run(611);
    check(!bus->dma_pending() && trace.empty(), "FIFO did not request DMA before the first sample pop");
    run(1024);
    const std::vector<std::int16_t> expected_pcm{0, 0, 8192, -4096};
    check(bus->take_audio() == expected_pcm, "two stereo samples, independent timers, pan and volume");
    constexpr TimingEvent expected[] = {
        {612, TimingKind::sound_fifo_request, 0}, {612, TimingKind::dma_start, 1}, {622, TimingKind::dma_complete, 1},
    };
    check(std::ranges::equal(trace, expected), "FIFO DMA transfers four words over ten ticks");
    run(9728);
    auto pcm = bus->take_audio();
    check(pcm[pcm.size() - 2] == 4096 && pcm.back() == -4096, "DMA refill reaches the DAC and an empty FIFO holds its sample");
    check(trace.size() == 6 && trace[3] == TimingEvent{8804, TimingKind::sound_fifo_request, 0},
          "the next request arrives sixteen pops after the first burst");
}

void replay() {
    const Fixture fixture;
    auto session = rse::session::create("gba", fixture.cart.bytes, fixture.bios.bytes);
    check(session->info().audio && session->info().audio->channels == 2 &&
          session->info().audio->sample_rate == 32768, "GBA stereo session metadata");
    constexpr Tick limit = 280896;
    session->advance_to(limit);
    auto reference = session->take_audio();
    check(reference.size() == 2 * (limit / 512) && std::ranges::any_of(reference, [](auto v) { return v != 0; }),
          "generated PSG and Direct Sound program produces PCM at the expected rate");
    session->reset();
    std::vector<std::int16_t> replayed;
    for (Tick tick = 0; tick < limit;) {
        tick = std::min(limit, tick + 1 + tick % 1973);
        session->advance_to(tick);
        const auto block = session->take_audio();
        replayed.insert(replayed.end(), block.begin(), block.end());
    }
    const auto hash = [](const auto& samples) {
        std::uint64_t value = 14695981039346656037ull;
        for (const auto sample : samples) {
            const auto bits = std::uint16_t(sample);
            value = (value ^ (bits & 255)) * 1099511628211ull;
            value = (value ^ (bits >> 8)) * 1099511628211ull;
        }
        return value;
    };
    constexpr std::uint64_t expected_hash = 0x7bde98df7a93717dull;
    check(replayed == reference && hash(replayed) == expected_hash && hash(reference) == expected_hash,
          "stereo PCM replay across reset and advance sizes");
    std::cout << "Stereo PCM hash " << std::hex << hash(reference) << '\n';
}

void generate(const std::filesystem::path& prefix) {
    const Fixture fixture;
    const auto save = [&](const char* suffix, const Bytes& bytes) {
        auto path = prefix;
        path += suffix;
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        file.close();
        check(bool(file), "could not write authored desktop fixture");
    };
    save(".gba", fixture.cart.bytes);
    save(".bin", fixture.bios.bytes);
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "generate") generate(argv[2]);
        else if (argc == 1) { psg_channels(); fifo_timing(); replay(); }
        else throw std::invalid_argument("Use generate <prefix> or omit options to run tests");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
