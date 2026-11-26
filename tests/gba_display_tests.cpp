// SPDX-License-Identifier: Apache-2.0
#include <rse/session/builtins.hpp>
#include <rse/systems/gba/session.hpp>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>

namespace {
using namespace rse::gba;
using rse::Tick;
using Bytes = std::vector<std::uint8_t>;
using rse::session::Session;
constexpr std::uint32_t io = 0x04000000, vram = 0x06000000, palette = 0x05000000, oam = 0x07000000;
constexpr std::uint16_t red = 0x001f, green = 0x03e0, blue = 0x7c00, backdrop = 0x4210;

void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

void put(Bytes& bytes, unsigned offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes.at(offset + i) = std::uint8_t(value >> (8 * i));
}

void idle(Bus& bus, Tick target) {
    while (bus.ticks() < target) bus.idle(target);
}

std::unique_ptr<Bus> fixture_bus() {
    return std::make_unique<Bus>(Bytes(bios_size), Bytes(cartridge_header_size));
}

std::unique_ptr<Session> keypad_program(bool continuous) {
    // Authored reset code configures mode 3 with an identity BG2 transform.
    // Each unrolled pair samples KEYINPUT at tick 18 + 4*n and stores the
    // 10-bit word as a pixel. The other program updates only pixel zero.
    Bytes bios(bios_size), rom(cartridge_header_size);
    constexpr unsigned literals = 0x200;
    constexpr unsigned registers[] = {0, 1, 3, 4, 5};
    constexpr std::uint32_t values[] = {io + 0x130, vram, io, 0x0403, 0x100};
    unsigned pc = 0;
    for (unsigned i = 0; i < std::size(values); ++i) {
        const unsigned address = literals + 4 * i;
        put(bios, pc, 0xe59f0000 | (registers[i] << 12) | (address - pc - 8));
        put(bios, address, values[i]);
        pc += 4;
    }
    put(bios, pc, 0xe1c352b0); // STRH r5,[r3,#0x20] (BG2PA)
    put(bios, pc + 4, 0xe1c352b6); // STRH r5,[r3,#0x26] (BG2PD)
    put(bios, pc + 8, 0xe1c340b0); // STRH r4,[r3] (DISPCNT)
    pc += 12;
    for (unsigned i = 0; i < (continuous ? 1u : 22u); ++i) {
        put(bios, pc, 0xe1d020b0); // LDRH r2,[r0]
        put(bios, pc + 4, continuous ? 0xe1c120b0 : 0xe0c120b2); // STRH r2,[r1]{,#2}
        pc += 8;
    }
    put(bios, pc, continuous ? 0xeafffffc : 0xeafffffe);
    return rse::session::create("gba", rom, bios);
}

std::uint16_t raw_pixel(const Session& session, unsigned index) {
    const auto frame = session.video();
    check(frame && frame->pitch_bytes == 480 && frame->pixels.size() == 480 * 160, "published BGR555 frame shape");
    std::uint16_t value{};
    std::memcpy(&value, frame->pixels.data() + index * 2, sizeof(value));
    return value;
}

void keypad() {
    auto session = keypad_program(false);
    const auto& info = session->info();
    check(info.video && info.video->width == 240 && info.video->height == 160 &&
          info.video->display_aspect == rse::session::Rational{3, 2} &&
          info.video->pixels == rse::session::PixelFormat::bgr555_u16_native && info.frame_step,
          "GBA dimensions, aspect and pixel format");
    constexpr std::array<std::string_view, 10> buttons{"a", "b", "select", "start", "right",
                                                      "left", "up", "down", "r", "l"};
    constexpr std::string_view port = "controller-1";
    check(info.input_ports.size() == 1 && info.input_ports.front().id == port &&
          std::ranges::equal(info.input_ports.front().actions, buttons), "all ten keypad action names");
    const std::array<std::string_view, 3> simultaneous{"a", "left", "l"};
    session->schedule_input(98, port, simultaneous);
    session->schedule_input(102, port, {});
    for (unsigned i = 1; i < buttons.size(); ++i) {
        session->schedule_input(18 + 8 * i, port, std::span(&buttons[i], 1));
        session->schedule_input(22 + 8 * i, port, {});
    }
    session->advance_to(17); // CPU fetch completed, KEYINPUT read still pending.
    check(!session->video() && session->status().tick == 17, "advance exposes the exact tick without publishing early");
    session->schedule_input(18, port, std::span(&buttons[1], 1)); // Earlier event at the same tick.
    session->schedule_input(18, port, std::span(&buttons[0], 1));
    session->schedule_input(22, port, {});
    auto invalid = [&](Tick tick, std::string_view name, std::span<const std::string_view> input) {
        try {
            session->schedule_input(tick, name, input);
        } catch (const std::invalid_argument&) {
            return;
        }
        throw std::runtime_error("invalid keypad event was accepted");
    };
    invalid(17, port, {});
    invalid(18, "controller-2", {});
    constexpr std::array<std::string_view, 1> unknown{"x"};
    constexpr std::array<std::string_view, 2> duplicate{"a", "a"};
    invalid(18, port, unknown);
    invalid(18, port, duplicate);

    session->run_frame();
    for (unsigned i = 0; i < buttons.size(); ++i) {
        check(raw_pixel(*session, 2 * i) == (0x3ff ^ (1u << i)), "individual key bit goes low at the read tick");
        check(raw_pixel(*session, 2 * i + 1) == 0x3ff, "released key bit returns high at the read tick");
    }
    check(raw_pixel(*session, 20) == 0x1de && raw_pixel(*session, 21) == 0x3ff,
          "simultaneous A, Left and L and complete release");
    session->schedule_input(session->status().tick + 1, port, buttons);
    session->reset();
    session->run_frame();
    check(raw_pixel(*session, 0) == 0x3ff && raw_pixel(*session, 20) == 0x3ff,
          "reset releases the keys and drops the future schedule");

    auto bus = fixture_bus();
    bus->store_half(io + 0x132, 0xc003); // IRQ on A and B.
    bus->schedule_keys(3, 1);
    bus->schedule_keys(5, 3);
    idle(*bus, 3);
    check(bus->load_half(io + 0x202) == 0, "AND keypad IRQ waits for the full chord");
    check(bus->load_half(io + 0x202) == 0x1000, "AND keypad IRQ latches on the chord");
    bus->store_half(io + 0x202, 0x1000);
    bus->schedule_keys(7, 3);
    check(bus->load_half(io + 0x202) == 0, "held chord does not immediately relatch an acknowledged IRQ");
    bus->store_half(io + 0x132, 0x4004); // OR, select only.
    bus->schedule_keys(9, 4);
    check(bus->load_half(io + 0x202) == 0x1000, "OR keypad IRQ matches an independently selected key");
}

std::uint64_t hash(const Session& session) {
    std::uint64_t result = 14695981039346656037ull;
    for (unsigned i = 0; i < screen_width * screen_height; ++i) {
        const auto pixel = raw_pixel(session, i);
        result = (result ^ (pixel & 255)) * 1099511628211ull;
        result = (result ^ (pixel >> 8)) * 1099511628211ull;
    }
    return result;
}

void frame_replay() {
    auto whole = keypad_program(true), chunked = keypad_program(true);
    constexpr std::array<std::string_view, 2> shoulders{"l", "r"};
    for (auto* session : {whole.get(), chunked.get()}) {
        session->schedule_input(280896 + 64, "controller-1", shoulders);
        session->schedule_input(2 * 280896 + 64, "controller-1", {});
    }
    constexpr Tick targets[] = {197120, 478016, 758912};
    constexpr std::uint16_t pixels[] = {0x3ff, 0xff, 0x3ff};
    constexpr std::uint64_t hashes[] = {0xcb8b539a481cde5full, 0x6941e3232f2803daull, 0xcb8b539a481cde5full};
    for (unsigned i = 0; i < std::size(targets); ++i) {
        whole->run_frame();
        while (chunked->status().tick < targets[i] - 1) {
            const auto now = chunked->status().tick;
            chunked->advance_to(std::min(targets[i] - 1, now + 1 + now % 1234));
        }
        check(chunked->status().video_frames == i, "frame is unpublished one tick before VBlank");
        chunked->advance_to(targets[i]);
        check(whole->status().tick == targets[i] && whole->video()->number == i + 1 &&
              chunked->video()->number == i + 1, "VBlank publication with 280896 ticks between frames");
        check(raw_pixel(*whole, 0) == pixels[i] && raw_pixel(*whole, 1) == 0 &&
              hash(*whole) == hashes[i] && hash(*chunked) == hashes[i],
              "input changes pixel zero on the expected frame across advance sizes");
    }
    std::vector<rse::session::Rgba> rgba(screen_width * screen_height);
    whole->convert_video(rgba);
    check(rgba[0] == rse::session::Rgba{255, 255, 0, 255} &&
          rgba[1] == rse::session::Rgba{0, 0, 0, 255}, "BGR555 conversion and channel order");
}

void timing() {
    auto bus = fixture_bus();
    std::vector<TimingEvent> trace;
    trace.reserve(32);
    bus->set_timing_sink([](void* context, TimingEvent event) noexcept {
        static_cast<std::vector<TimingEvent>*>(context)->push_back(event);
    }, &trace);
    bus->store_half(io + 4, 8); // VBlank IRQ.
    bus->store_half(io + 0x200, 0x0319);
    bus->store_half(io + 0x208, 1);
    bus->store_word(io + 0x104, 0x00c4ffff); // Timer 1 cascades and overflows on the first Timer 0 wrap.
    bus->store_word(0x03000000, 0x98765432);
    bus->store_word(io + 0xb0, 0x03000000);
    bus->store_word(io + 0xb4, 0x03000010);
    bus->store_word(io + 0xb8, 0xd0000001);
    bus->store_word(io + 0xbc, 0x03000002);
    bus->store_word(io + 0xc0, 0x03000012);
    bus->store_word(io + 0xc4, 0xd0000001);
    idle(*bus, 196999);
    // Enabled at 197000. The next two divide-by-64 increments are 197056 and 197120.
    bus->store_word(io + 0x100, 0x00c1fffe);
    idle(*bus, 197118);
    check(bus->load_half(io + 0x100) == 0xffff && !bus->irq() && !bus->dma_pending(),
          "timer counter and no IRQ or DMA before VBlank");
    check((bus->load_half(io + 4) & 1) && bus->ticks() == 197120 && bus->irq() && bus->dma_pending(),
          "VBlank, timer IRQ and DMA requests become visible together");
    bus->step_dma(197122); // Pause during DMA 0 startup, with DMA 1 queued on the same edge.
    bus->step_dma();
    bus->step_dma();
    check(bus->ticks() == 197128 && !bus->dma_pending(), "DMA 0 runs to completion before DMA 1");
    check(bus->load_word(0x03000010) == 0x98765432 && bus->load_half(io + 0x202) == 0x0319,
          "both DMA writes and all requested IF bits are visible");
    bus->store_half(io + 0x202, 0x0119);
    check(bus->irq(), "acknowledging one channel preserves another channel's IRQ");
    bus->store_byte(io + 0x203, 2);
    check(!bus->irq(), "all enabled IRQ sources are acknowledged");
    bus->store_half(io + 0x102, 0);
    check(bus->load_half(io + 0x100) == 0xfffe, "stopping the timer freezes the reloaded counter");
    bus->store_half(io + 0x100, 0x1234);
    check(bus->load_half(io + 0x100) == 0xfffe, "a reload write does not change the stopped counter");
    idle(*bus, 279662);
    check(bus->load_half(io + 4) & 1, "VBlank remains set through line 226");
    check(!(bus->load_half(io + 4) & 1) && bus->ticks() == 279664, "VBlank exits at line 227");
    constexpr TimingEvent expected[] = {
        {197120, TimingKind::vblank_enter}, {197120, TimingKind::frame},
        {197120, TimingKind::timer_overflow, 0}, {197120, TimingKind::timer_overflow, 1},
        {197120, TimingKind::dma_start, 0}, {197124, TimingKind::dma_complete, 0},
        {197124, TimingKind::dma_start, 1}, {197128, TimingKind::dma_complete, 1},
        {197131, TimingKind::irq_acknowledge, 0x0119}, {197132, TimingKind::irq_acknowledge, 0x0200},
        {279664, TimingKind::vblank_exit},
    };
    check(std::ranges::equal(trace, expected), "exact ordered trace for coincident LCD, timer, DMA and IRQ events");
}

void dma_preemption() {
    auto bus = fixture_bus();
    std::vector<TimingEvent> dma;
    dma.reserve(8);
    bus->set_timing_sink([](void* context, TimingEvent event) noexcept {
        if (event.kind == TimingKind::dma_start || event.kind == TimingKind::dma_complete)
            static_cast<std::vector<TimingEvent>*>(context)->push_back(event);
    }, &dma);
    bus->store_word(0x02000000, 0x11223344);
    bus->store_word(0x02000004, 0x55667788);
    bus->store_word(io + 0xd4, 0x02000000);
    bus->store_word(io + 0xd8, 0x03000000);
    bus->store_word(io + 0xb0, 0x03000000);
    bus->store_word(io + 0xb4, 0x03000008);
    bus->store_word(io + 0xb8, 0x94000001); // DMA0, VBlank, word.
    idle(*bus, 197115);
    bus->store_word(io + 0xdc, 0x84000002); // DMA3, immediate, two words.
    bus->step_dma(197120);
    while (bus->dma_pending()) bus->step_dma();
    constexpr TimingEvent expected[] = {
        {197116, TimingKind::dma_start, 3}, {197125, TimingKind::dma_start, 0},
        {197129, TimingKind::dma_complete, 0}, {197129, TimingKind::dma_start, 3},
        {197138, TimingKind::dma_complete, 3},
    };
    check(std::ranges::equal(dma, expected), "higher priority DMA preempts at the next read/write boundary");
    check(bus->load_word(0x03000008) == 0x11223344 && bus->load_word(0x03000004) == 0x55667788,
          "arbitration preserves the interrupted channel's address progress");
}

void bitmaps() {
    auto bus = fixture_bus();
    const auto pixel = [&](unsigned x, unsigned y = 0) { return bus->display().frame()[screen_width * y + x]; };
    idle(*bus, first_frame_tick);
    check(pixel(0) == 0x7fff && pixel(239, 159) == 0x7fff, "forced blank outputs white");
    bus->store_half(io + 0x20, 0x100);
    bus->store_half(io + 0x26, 0x100);
    bus->store_half(palette, backdrop);
    bus->store_half(io, 0x0403);
    bus->store_word(vram, (std::uint32_t(blue) << 16) | red);
    bus->store_half(vram + 480, green);
    idle(*bus, first_frame_tick + frame_ticks);
    check(pixel(0) == red && pixel(1) == blue && pixel(0, 1) == green &&
          pixel(2) == 0, "mode 3 uses direct color and advances affine rows");
    bus->store_half(io, 0x0414);
    bus->store_half(palette + 2, blue);
    bus->store_half(vram + 0xa000, 0x0001);
    idle(*bus, first_frame_tick + 2 * frame_ticks);
    check(pixel(0) == blue && pixel(1) == backdrop, "mode 4 uses the selected page and transparent palette index zero");
    bus->store_half(io, 0x0415);
    bus->store_half(vram + 0xa000 + (127 * 160 + 159) * 2, green);
    idle(*bus, first_frame_tick + 3 * frame_ticks);
    check(pixel(159, 127) == green && pixel(160, 127) == backdrop &&
          pixel(0, 128) == backdrop, "mode 5 page and 160x128 bitmap bounds");
}

void hblank_raster() {
    auto bus = fixture_bus();
    bus->store_half(io, 0);
    bus->store_half(palette, red);
    bus->store_half(0x03000000, green);
    bus->store_word(io + 0xb0, 0x03000000);
    bus->store_word(io + 0xb4, palette);
    bus->store_word(io + 0xb8, 0xa0000001); // One HBlank halfword.
    idle(*bus, hblank_tick);
    check(bus->dma_pending(), "HBlank requests DMA");
    bus->step_dma();
    idle(*bus, first_frame_tick);
    const auto& frame = bus->display().frame();
    check(frame[0] == red && frame[screen_width - 1] == red &&
          frame[screen_width] == green && frame.back() == green,
          "HBlank DMA updates the backdrop for the following scanline");
}

void tiles_and_objects() {
    auto bus = fixture_bus();
    const auto pixel = [&](unsigned x) { return bus->display().frame()[x]; };
    bus->store_half(palette, backdrop);
    bus->store_half(palette + 2, blue);
    bus->store_half(palette + 34, red);
    bus->store_half(palette + 514, green);
    bus->store_half(palette + 546, blue);
    bus->store_half(vram + 0x20, 0x1101); // Text 4bpp tile 1, second pixel transparent.
    bus->store_half(vram + 0x22, 0x1111);
    bus->store_word(vram + 0x4040, 0x01010101); // Text 8bpp tile 1 in character block 1.
    bus->store_word(vram + 0x4044, 0x01010101);
    for (unsigned offset : {0u, 4u, 8u}) {
        bus->store_word(vram + 0xf800 + offset, 0x10011001);
        bus->store_word(vram + 0xf000 + offset, 0x00010001);
    }
    bus->store_half(io + 8, 0x1f01);
    bus->store_half(io + 10, 0x1e85);
    bus->store_word(vram + 0x10020, 0x11111111);
    bus->store_word(oam, 0x00080400); // Semi-transparent OBJ at x=8.
    bus->store_half(oam + 4, 0x0401);
    bus->store_word(oam + 8, 0x00080000); // Same priority and x, later OAM index.
    bus->store_half(oam + 12, 0x1401);
    bus->store_word(oam + 16, 0x00140800); // OBJ window at x=20.
    bus->store_half(oam + 20, 1);
    bus->store_half(io + 0x40, 0x000c); // WIN0 x=[0,12)
    bus->store_half(io + 0x42, 0x0414); // WIN1 x=[4,20)
    bus->store_word(io + 0x44, 0x00020002);
    bus->store_half(io + 0x48, 0x0233); // WIN0 enables OBJ and effects, WIN1 only BG1.
    bus->store_half(io + 0x4a, 0x0203); // OBJ window only BG1, outside both BGs.
    bus->store_half(io + 0x50, 0x0100); // BG0 is the only second target.
    bus->store_half(io + 0x52, 0x0808);
    bus->store_half(io, 0xf300); // Mode 0, BG0, BG1, OBJ and all three windows.
    idle(*bus, first_frame_tick);
    check(pixel(0) == red && pixel(1) == blue && pixel(4) == red, "BG tie, text transparency and WIN0 over WIN1");
    check(pixel(8) == 0x01ef && pixel(9) == green, "OBJ wins BG/OAM ties and blends only with a selected next pixel");
    check(pixel(12) == blue && pixel(20) == blue && pixel(28) == red && pixel(48) == backdrop,
          "WIN1, OBJ window, outside region and backdrop");

    bus->store_half(io, 0x1300);
    bus->store_half(io + 8, 0x1f00); // BG0 now has higher priority than the OBJs.
    bus->store_half(io + 0x50, 0x0241); // BG0 over BG1 alpha.
    idle(*bus, first_frame_tick + frame_ticks);
    check(pixel(0) == 0x3c0f && pixel(1) == blue && pixel(8) == red && pixel(9) == 0x3de0,
          "BG alpha uses the adjacent target and respects an intervening OBJ");
    bus->store_half(io, 0x0300);
    bus->store_half(io + 0x50, 0x0081);
    bus->store_half(io + 0x54, 8);
    idle(*bus, first_frame_tick + 2 * frame_ticks);
    check(pixel(0) == 0x3dff && pixel(1) == blue, "brightness increase selects only the programmed layer");
    bus->store_half(io + 0x50, 0x00c1);
    idle(*bus, first_frame_tick + 3 * frame_ticks);
    check(pixel(0) == 0x0010, "brightness decrease");

    bus->store_half(io, 0x0402); // Mode 2 affine BG2.
    bus->store_half(io + 0xc, 0x3f00); // Screen block 31, wrap enabled.
    bus->store_half(io + 0x20, 0x100);
    bus->store_half(io + 0x26, 0x100);
    bus->store_word(io + 0x28, 0x0fffff00); // x=-1, y=0.
    bus->store_half(vram + 0xf80e, 0x0200); // Map entry 15 references tile 2.
    bus->store_half(vram + 0x86, 0x0100); // Tile 2 column 7 is palette index 1.
    idle(*bus, first_frame_tick + 4 * frame_ticks);
    check(pixel(0) == blue && pixel(1) == backdrop, "negative affine reference wraps to the last tile");
}

void sprite_geometry() {
    auto bus = fixture_bus();
    bus->store_half(io, 0x1040); // OBJ, 1D tile mapping.
    bus->store_half(palette, backdrop);
    bus->store_half(palette + 514, green);
    bus->store_half(palette + 516, red);
    bus->store_word(vram + 0x10040, 0x01010101);
    bus->store_word(vram + 0x10044, 0x01010101);
    bus->store_word(vram + 0x10080, 0x02020202);
    bus->store_word(vram + 0x10084, 0x02020202);
    bus->store_word(oam, 0x10046000); // 16x8, 8bpp, flipped horizontally, at x=4.
    bus->store_half(oam + 4, 2);
    bus->store_word(oam + 8, 0x001c2300); // 8x8 affine with a 16x16 bounding box at x=28.
    bus->store_half(oam + 12, 2);
    bus->store_half(oam + 6, 0x100); // Affine matrix 0 PA.
    bus->store_half(oam + 30, 0x100); // Affine matrix 0 PD.
    idle(*bus, first_frame_tick);
    const auto& frame = bus->display().frame();
    const auto row = 4 * screen_width;
    check(frame[3] == backdrop && frame[4] == red && frame[12] == green, "flipped 8bpp OBJ with 1D tile stride");
    check(frame[row + 31] == backdrop && frame[row + 32] == green && frame[row + 40] == backdrop,
          "double-size affine OBJ is centered and clipped to its source");
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::invalid_argument("Specify keypad, frame-replay, timing or scenes");
        const std::string_view suite = argv[1];
        if (suite == "keypad") keypad();
        else if (suite == "frame-replay") frame_replay();
        else if (suite == "timing") { timing(); dma_preemption(); }
        else if (suite == "scenes") { bitmaps(); hblank_raster(); tiles_and_objects(); sprite_geometry(); }
        else throw std::invalid_argument("Unknown GBA display fixture");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
