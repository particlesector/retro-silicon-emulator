// SPDX-License-Identifier: Apache-2.0
#include "ppu_test_bus.hpp"
#include <algorithm>
#include <iostream>

namespace {
using rse::video::Rp2c02;
void registers() {
    PpuTestBus bus;
    Rp2c02 ppu(bus);
    check(ppu.state().scanline == 261 && ppu.state().dot == 0 && !ppu.nmi_asserted(), "initial position/NMI");
    ppu.cpu_write(0, 0x83);
    check(ppu.state().t == 0x0c00, "control nametable bits");
    ppu.cpu_write(0x2005, 0x2d);
    check(ppu.state().fine_x == 5 && ppu.state().t == 0x0c05 && ppu.state().write_toggle, "first scroll write");
    ppu.cpu_write(5, 0x9e);
    check(ppu.state().t == 0x6e65 && !ppu.state().write_toggle, "second scroll write");
    ppu.cpu_write(5, 0x11);
    check((ppu.cpu_read(2) & 31) == 0x11 && !ppu.state().write_toggle, "status open bus/write toggle");
    ppu.cpu_write(6, 0xff);
    check(ppu.state().t == 0x3f62 && ppu.state().write_toggle, "address high six bits/bit14 clear");
    ppu.cpu_write(6, 0xfe);
    check(ppu.state().v == 0x3ffe && !ppu.state().write_toggle, "address second write");
    ppu.cpu_write(2, 0x9b);
    check(ppu.cpu_read(0) == 0x9b && ppu.cpu_read(1) == 0x9b, "write-only ports must read I/O latch");
    check((ppu.state().status & 0xe0) == 0, "status writes must not set flags");
}
void data_ports() {
    PpuTestBus bus;
    bus.ram[0x2000] = 0x12; bus.ram[0x2001] = 0x34;
    bus.ram[0x2f00] = 0x56;
    Rp2c02 ppu(bus);
    address(ppu, 0x2000);
    const auto prior=bus.trace.size();
    check(ppu.cpu_peek(7)==0 && ppu.cpu_peek(7)==0 && ppu.state().v==0x2000 &&
        bus.trace.size()==prior,"PPUDATA peek changed address, buffer or bus");
    check(ppu.cpu_read(7) == 0 && ppu.cpu_read(7) == 0x12 && ppu.cpu_read(7) == 0x34,
          "data reads must be buffered");
    check(ppu.state().v == 0x2003, "data read increment");
    memory(ppu, 0x3f10, 0xff);
    address(ppu, 0x3f00);
    ppu.cpu_write(2, 0xc0);
    check(ppu.cpu_peek(7)==0xff && ppu.state().v==0x3f00,"palette peek changed PPUDATA");
    check(ppu.cpu_read(7) == 0xff, "palette alias/six-bit data/open-bus high bits");
    address(ppu, 0x2000);
    check(ppu.cpu_read(7) == 0x56, "palette read must refill buffer from shadow nametable");
    ppu.cpu_write(1, 1);
    address(ppu, 0x3f10);
    check((ppu.cpu_read(7) & 63) == 0x30, "palette reads apply grayscale");
    ppu.cpu_write(1, 0);
    for (unsigned i : {0u, 4u, 8u, 12u}) {
        memory(ppu, 0x3f00 + i, static_cast<std::uint8_t>(i + 1));
        address(ppu, 0x3f30 + i);
        check((ppu.cpu_read(7) & 63) == i + 1, "palette repeats and universal-color aliases");
    }
    ppu.cpu_write(0, 4);
    address(ppu, 0x2100); ppu.cpu_write(7, 0x77); ppu.cpu_write(7, 0x88);
    check(bus.ram[0x2100] == 0x77 && bus.ram[0x2120] == 0x88 && ppu.state().v == 0x2140,
          "increment-32 writes");
    ppu.cpu_write(0, 0);
    address(ppu, 0x3fff); ppu.cpu_write(7, 1);
    check(ppu.state().v == 0x4000, "15-bit VRAM increment");
    ppu.cpu_write(7, 0xab);
    check(bus.ram[0] == 0xab && ppu.state().v == 0x4001, "external memory addresses are 14-bit");
    check(std::all_of(bus.trace.begin(), bus.trace.end(), [](auto access) { return access.dot == 0; }),
          "register transactions must not implicitly advance time");
}
void oam_ports() {
    PpuTestBus bus;
    Rp2c02 ppu(bus);
    ppu.cpu_write(3, 0xfe); ppu.cpu_write(4, 0xff); ppu.cpu_write(4, 0x42); ppu.cpu_write(4, 0x99);
    check(ppu.state().oam_address == 1, "OAM write increment/wrap");
    ppu.cpu_write(3, 0xfe);
    check(ppu.cpu_read(4) == 0xe3 && ppu.state().oam_address == 0xfe, "OAM attribute bits/read no increment");
    ppu.cpu_write(3, 0xff); check(ppu.cpu_read(4) == 0x42, "OAM boundary value");
    ppu.cpu_write(3, 0); check(ppu.cpu_read(4) == 0x99, "OAM wrap value");
    ppu.cpu_write(1, 8);
    until(ppu, 0, 2);
    check(ppu.cpu_read(4) == 0xff, "OAM reads during secondary clear");
    ppu.cpu_write(3, 0); ppu.cpu_write(4, 0x11);
    check(ppu.state().oam_address == 4, "rendering OAM write increments high six bits");
    ppu.cpu_write(1, 0); clocks(ppu, 3); ppu.cpu_write(3, 0);
    check(ppu.cpu_read(4) == 0x99, "rendering OAM writes must not alter primary RAM");
    // Nonzero OAMADDR at evaluation start copies the addressed row to row 0.
    ppu.reset();
    ppu.cpu_write(3, 0x28);
    for (auto byte : {10,11,2,13,20,21,3,23}) ppu.cpu_write(4, static_cast<std::uint8_t>(byte));
    ppu.cpu_write(1, 8); until(ppu, 0, 65);
    ppu.cpu_write(3, 0x28); ppu.clock(); ppu.cpu_write(1, 0); clocks(ppu, 3);
    unsigned i = 0;
    for (auto expected : {10,11,2,13,20,21,3,23}) {
        ppu.cpu_write(3, static_cast<std::uint8_t>(i++));
        check(ppu.cpu_read(4) == expected, "OAM refresh row copy");
    }
}
void timing_and_nmi() {
    for (bool render : {false, true}) {
        PpuTestBus bus; bus.recording = false;
        Rp2c02 ppu(bus);
        if (render) ppu.cpu_write(1, 8);
        const auto first = ppu.state().dots;
        clocks(ppu, 341 * 262);
        check(ppu.state().scanline == 261 && ppu.state().dot == 0 && ppu.state().odd,
              "even frame position/parity");
        check(ppu.state().dots - first == 89342, "even frame length");
        const auto second = ppu.state().dots;
        clocks(ppu, render ? 89341 : 89342);
        check(ppu.state().scanline == 261 && ppu.state().dot == 0 && !ppu.state().odd,
              "odd frame skip/parity");
        check(ppu.state().dots - second == (render ? 89341 : 89342), "odd frame length");
    }
    PpuTestBus bus; bus.recording = false;
    Rp2c02 ppu(bus);
    ppu.cpu_write(0, 0x80);
    until(ppu, 241, 1);
    check(!ppu.nmi_asserted() && ppu.state().frames == 0, "vblank/NMI set too early");
    ppu.clock();
    check(ppu.nmi_asserted() && ppu.frame().number == 1 && ppu.state().frames == 1, "vblank/NMI publication");
    ppu.cpu_write(0, 0);
    check(!ppu.nmi_asserted(), "disabling NMI must lower output immediately");
    ppu.cpu_write(0, 0x80);
    check(ppu.nmi_asserted(), "enabling NMI during vblank must assert output");
    check(ppu.cpu_read(2) & 0x80, "status must return then clear vblank");
    check(!ppu.nmi_asserted() && !(ppu.cpu_read(2) & 0x80), "status clear/NMI deassert");
    for (unsigned dot : {0, 1}) {
        ppu.reset(); ppu.cpu_write(0, 0x80);
        until(ppu, 241, dot); (void)ppu.cpu_read(2);
        until(ppu, 241, 2);
        check(!ppu.nmi_asserted() && !(ppu.state().status & 0x80) && ppu.frame().number == 1,
              "status read at vblank boundary must suppress flag/NMI, not publication");
    }
    ppu.reset(); ppu.cpu_write(0, 0x80);
    until(ppu, 241, 2);
    until(ppu, 261, 1);
    check(ppu.nmi_asserted(), "vblank cleared before pre-render dot 1");
    ppu.clock(); check(!ppu.nmi_asserted() && !(ppu.state().status & 0xe0), "pre-render clears status flags");
}
void render_enable_pipeline() {
    PpuTestBus bus;
    Rp2c02 ppu(bus);
    ppu.cpu_write(1, 8); clocks(ppu, 3);
    check(bus.trace.empty(), "rendering enable took effect before three dots");
    clocks(ppu, 2);
    check(bus.trace == std::vector<PpuAccess>{{0x23c0,0,false,5}}, "rendering enable pipeline boundary");
    ppu.cpu_write(1, 0); clocks(ppu, 3); ppu.clock();
    check(bus.trace == std::vector<PpuAccess>{{0x23c0,0,false,5},{0,0,false,7}},
          "rendering disable must drain its three-dot pipeline");
    for (unsigned enable_dot : {336,337}) {
        ppu.reset(); bus.recording = false;
        clocks(ppu,341*262);
        const auto start = ppu.state().dots;
        until(ppu,261,enable_dot); ppu.cpu_write(1,8); until(ppu,0,0);
        check(ppu.state().dots-start == (enable_dot==336 ? 340 : 341),
              "late rendering enable/odd-frame skip boundary");
    }
    ppu.reset(); ppu.cpu_write(1,8); clocks(ppu,341*262);
    const auto start = ppu.state().dots;
    until(ppu,261,338); ppu.cpu_write(1,0); until(ppu,0,0);
    check(ppu.state().dots-start == 340, "late rendering disable must not cancel already-enabled skip");
}
void scroll_and_fetches() {
    PpuTestBus bus;
    bus.ram[0x2002] = 3; bus.ram[0x23c0] = 0xe4;
    bus.ram[0x30] = 0x12; bus.ram[0x38] = 0x34;
    Rp2c02 ppu(bus);
    ppu.cpu_write(1, 8);
    until(ppu, 0, 1); bus.trace.clear(); clocks(ppu, 8);
    check(bus.trace == std::vector<PpuAccess>{{0x2002,3,false,344}, {0x23c0,0xe4,false,346},
                                             {0x30,0x12,false,348}, {0x38,0x34,false,350}}, "background fetch address/timestamp reference");
    check((ppu.state().v & 31) == 3, "coarse X increments at dot 8");
    ppu.cpu_write(5, 0xf8); ppu.cpu_write(5, 0xef);
    until(ppu, 261, 257); ppu.clock();
    check((ppu.state().v & 0x041f) == 31, "horizontal scroll reload");
    until(ppu, 261, 305);
    check((ppu.state().v & 0x7be0) == (ppu.state().t & 0x7be0), "pre-render vertical reload");
    until(ppu, 0, 256); ppu.clock();
    check((ppu.state().v & 0x7be0) == 0x0800, "fine Y/coarse Y 29 nametable wrap");
    // Rendering-time PPUDATA increments X and Y, independent of increment bit.
    address(ppu, 0x001f);
    (void)ppu.cpu_read(7);
    check(ppu.state().v == 0x1400, "rendering-time data address increment");
    address(ppu, 0x03ff); // coarse Y=31, fine Y=0; set fine Y=7 via scroll reload.
    ppu.cpu_write(5, 0); ppu.cpu_write(5, 0xff);
    until(ppu, 261, 305);
    until(ppu, 0, 256); ppu.clock();
    check((ppu.state().v & 0x7be0) == 0, "coarse Y 31 wraps without toggling nametable");
    ppu.reset(); ppu.cpu_write(1, 8);
    until(ppu, 261, 337); bus.trace.clear(); clocks(ppu, 4);
    check(bus.trace == std::vector<PpuAccess>{{0x2002,3,false,339}, {0x2002,3,false,341}},
          "trailing nametable fetch address/timestamps");
}
void faults_and_reset() {
    PpuTestBus bus;
    Rp2c02 ppu(bus);
    memory(ppu, 0x2000, 0x77);
    memory(ppu, 0x3f00, 0x22);
    bus.fail = true;
    address(ppu, 0x2000);
    bool failed = false;
    try { (void)ppu.cpu_read(7); }
    catch (const std::runtime_error& error) { failed = std::string(error.what()) == "PPU memory failure"; }
    check(failed && ppu.state().faulted, "bus failure propagation/fault");
    bool blocked = false;
    try { ppu.clock(); } catch (const std::logic_error&) { blocked = true; }
    check(blocked, "faulted PPU must reject clocks");
    bus.fail = false; ppu.reset();
    check(ppu.state().dots == 0 && !ppu.state().faulted && bus.ram[0x2000] == 0x77, "reset timeline/external memory preservation");
    address(ppu, 0x3f00); check((ppu.cpu_read(7) & 63) == 0, "reset palette state");
    const rse::video::PpuFrame empty;
    check(ppu.frame().samples == empty.samples && ppu.frame().number == 0, "initial frame must be zero-filled");
    ppu.reset(); ppu.cpu_write(1, 8); clocks(ppu, 4);
    bus.fail = true;
    failed = false;
    try { ppu.clock(); } catch (const std::runtime_error&) { failed = true; }
    check(failed && ppu.state().faulted && ppu.state().dots == 4, "rendering fetch failure/completed dot count");
    blocked = false;
    try { ppu.cpu_write(0, 0x80); } catch (const std::logic_error&) { blocked = true; }
    check(blocked, "faulted PPU must reject register writes");
    bus.fail = false; ppu.reset(); clocks(ppu, 3);
    check(!ppu.state().faulted, "rendering fetch failure reset recovery");
}
}
int main() {
    try { registers(); data_ports(); oam_ports(); timing_and_nmi(); render_enable_pipeline(); scroll_and_fetches(); faults_and_reset(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
