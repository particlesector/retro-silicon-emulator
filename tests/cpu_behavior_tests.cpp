// SPDX-License-Identifier: Apache-2.0
#include "cpu_test_bus.hpp"
#include <rse/systems/nes/bus.hpp>
#include <rse/systems/nes/cpu.hpp>
#include <algorithm>
#include <iostream>
#include <set>
#include <sstream>

namespace {
using rse::cpu::CpuState;
using rse::cpu::Mos6502;
using rse::cpu::Variant;
constexpr std::uint8_t C = 1, Z = 2, I = 4, D = 8, B = 16, U = 32, V = 64, N = 128;

void trace(const TestBus& bus, const std::vector<Access>& expected) {
    check(bus.trace.size() == expected.size(), "trace length mismatch");
    for (std::size_t i = 0; i < expected.size(); ++i)
        check(bus.trace[i] == expected[i], "trace mismatch at cycle " + std::to_string(expected[i].cycle));
}
void setup(TestBus& bus) {
    bus.ram.fill(0xea);
    bus.ram[0xfffc] = 0x00; bus.ram[0xfffd] = 0x80;
    bus.ram[0xfffe] = 0x00; bus.ram[0xffff] = 0x90;
    bus.ram[0xfffa] = 0x00; bus.ram[0xfffb] = 0xa0;
}
void reset_cases() {
    for (auto variant : {Variant::nmos6502, Variant::ricoh2a03}) {
        TestBus bus;
        setup(bus);
        Mos6502 cpu(bus, variant);
        check(!cpu.at_instruction_boundary(), "power-on reset must be pending");
        step(cpu);
        trace(bus, {{0,0xea,false,1}, {0,0xea,false,2}, {0x100,0xea,false,3},
                    {0x1ff,0xea,false,4}, {0x1fe,0xea,false,5},
                    {0xfffc,0,false,6}, {0xfffd,0x80,false,7}});
        check(cpu.state() == CpuState{0x8000,0xfd,0,0,0,0x24}, "power-on reset state");
        bus.trace.clear();
        cpu.reset();
        step(cpu);
        check(cpu.cycles() == 14 && cpu.state().s == 0xfa, "reset cycle count/stack decrement");

        TestBus warm;
        setup(warm);
        Mos6502 other(warm, CpuState{0x3456,1,0x12,0x34,0x56,0x69}, variant);
        other.reset();
        step(other);
        trace(warm, {{0x3456,0xea,false,1}, {0x3456,0xea,false,2},
                     {0x101,0xea,false,3}, {0x100,0xea,false,4}, {0x1ff,0xea,false,5},
                     {0xfffc,0,false,6}, {0xfffd,0x80,false,7}});
        check(other.state() == CpuState{0x8000,0xfe,0x12,0x34,0x56,0x6d}, "warm reset preservation");
        // Reset cancels an in-flight absolute load rather than finishing it.
        warm.ram[0x8000] = 0xad;
        other.clock(); other.clock();
        const auto cycles_before = other.cycles();
        other.reset();
        step(other);
        check(other.cycles() == cycles_before + 7 && other.state().a == 0x12,
              "mid-instruction reset failed to cancel pending access");
    }
}

void irq_and_nmi() {
    TestBus bus;
    setup(bus);
    Mos6502 cpu(bus, CpuState{0x8000,0xfd,0,0,0,0x20});
    cpu.set_irq(true);
    step(cpu); // NOP polls IRQ at its first cycle.
    check(cpu.cycles() == 2 && cpu.state().pc == 0x8001, "IRQ should finish current instruction");
    cpu.set_irq(false); // The accepted request must survive deassertion.
    bus.trace.clear();
    step(cpu);
    trace(bus, {{0x8001,0xea,false,3}, {0x8001,0xea,false,4},
                {0x1fd,0x80,true,5}, {0x1fc,1,true,6}, {0x1fb,0x20,true,7},
                {0xfffe,0,false,8}, {0xffff,0x90,false,9}});
    check(cpu.state().pc == 0x9000 && cpu.state().s == 0xfa && cpu.state().p == 0x24,
          "IRQ entry state");

    TestBus nmi_bus;
    setup(nmi_bus);
    Mos6502 nmi(nmi_bus, CpuState{0x8000,0xfd,0,0,0,0x20});
    nmi.set_irq(true); nmi.set_nmi(true);
    step(nmi); nmi_bus.trace.clear(); step(nmi);
    trace(nmi_bus, {{0x8001,0xea,false,3}, {0x8001,0xea,false,4},
                    {0x1fd,0x80,true,5}, {0x1fc,1,true,6}, {0x1fb,0x20,true,7},
                    {0xfffa,0,false,8}, {0xfffb,0xa0,false,9}});
    check(nmi.state().pc == 0xa000, "NMI must win over IRQ");
    step(nmi); step(nmi);
    check(nmi.state().pc == 0xa002, "held NMI must not retrigger");
    nmi.set_nmi(false); nmi.set_nmi(true);
    step(nmi);
    check(nmi.state().pc == 0xa003, "unsampled NMI pulse must not retrigger");
    nmi.set_nmi(false); step(nmi);
    nmi.set_nmi(true); step(nmi); step(nmi);
    check(nmi.state().pc == 0xa000, "a newly sampled edge must retrigger NMI");

    TestBus pulse_bus;
    setup(pulse_bus);
    Mos6502 pulse(pulse_bus, CpuState{0x8000,0xfd,0,0,0,0x24});
    pulse_bus.on_cycle = [&](auto cycle) { pulse.set_nmi(cycle == 1); };
    step(pulse); step(pulse);
    check(pulse.state().pc == 0xa000, "sampled NMI pulse must remain latched despite I");
}

void interrupt_latency() {
    for (auto opcode : {0x58, 0x78, 0x28}) { // CLI, SEI, PLP
        for (bool initially_masked : {false, true}) {
            TestBus bus;
            setup(bus);
            bus.ram[0x8000] = static_cast<std::uint8_t>(opcode);
            // PLP flips the mask, exercising both enable and disable latency.
            bus.ram[0x1fd] = initially_masked ? 0x20 : 0x24;
            Mos6502 cpu(bus, CpuState{0x8000,0xfc,0,0,0,
                                     static_cast<std::uint8_t>(initially_masked ? 0x24 : 0x20)});
            cpu.set_irq(true);
            step(cpu);
            if (!initially_masked) {
                step(cpu);
                check(cpu.state().pc == 0x9000, "old I=0 must allow IRQ after CLI/SEI/PLP");
            } else {
                step(cpu);
                check(cpu.state().pc == 0x8002, "old I=1 must defer IRQ for one instruction");
                if (opcode != 0x78) {
                    step(cpu);
                    check(cpu.state().pc == 0x9000, "unmasked IRQ must be serviced after delay");
                }
            }
        }
    }
    TestBus rti_bus;
    setup(rti_bus);
    rti_bus.ram[0x8000] = 0x40;
    rti_bus.ram[0x1fb] = 0x20;
    rti_bus.ram[0x1fc] = 0x34; rti_bus.ram[0x1fd] = 0x12;
    Mos6502 rti(rti_bus, CpuState{0x8000,0xfa,0,0,0,0x24});
    rti.set_irq(true);
    step(rti);
    check(rti.state().pc == 0x1234 && rti.state().p == 0x20, "RTI restored state");
    step(rti);
    check(rti.state().pc == 0x9000 && rti_bus.ram[0x1fc] == 0x34, "RTI must use restored I immediately");

    // An IRQ first asserted on the last cycle misses the current poll.
    TestBus last_bus;
    setup(last_bus);
    Mos6502 late(last_bus, CpuState{0x8000,0xfd,0,0,0,0x20});
    last_bus.on_cycle = [&](auto cycle) { if (cycle == 2) late.set_irq(true); };
    step(late); step(late);
    check(late.state().pc == 0x8002, "last-cycle IRQ accepted too early");
    step(late);
    check(late.state().pc == 0x9000, "last-cycle IRQ was lost");
}

void branch_polling() {
    for (bool crossing : {false, true}) {
        for (unsigned assertion : {1, 2}) {
            TestBus bus;
            setup(bus);
            const std::uint16_t pc = crossing ? 0x80fd : 0x8000;
            bus.ram[pc] = 0xd0; bus.ram[pc + 1] = 3;
            Mos6502 cpu(bus, CpuState{pc,0xfd,0,0,0,0x20});
            bus.on_cycle = [&](auto cycle) { if (cycle == assertion) cpu.set_irq(true); };
            step(cpu);
            check(cpu.cycles() == (crossing ? 4 : 3), "taken branch cycles");
            const auto target = cpu.state().pc;
            step(cpu);
            if (crossing || assertion == 1)
                check(cpu.state().pc == 0x9000, "branch poll did not accept IRQ");
            else {
                check(cpu.state().pc == target + 1, "same-page branch polled its third cycle");
                step(cpu);
                check(cpu.state().pc == 0x9000, "deferred branch IRQ was lost");
            }
        }
    }
    // The first branch poll remains accepted even if IRQ drops before fixup.
    TestBus bus;
    setup(bus);
    bus.ram[0x80fd] = 0xd0; bus.ram[0x80fe] = 3;
    Mos6502 cpu(bus, CpuState{0x80fd,0xfd,0,0,0,0x20});
    bus.on_cycle = [&](auto cycle) { cpu.set_irq(cycle == 1); };
    step(cpu); step(cpu);
    check(cpu.state().pc == 0x9000, "crossing branch overwrote accepted IRQ");
}

void hijacking() {
    for (bool hardware : {false, true}) for (bool early : {false, true}) {
        TestBus bus;
        setup(bus);
        bus.ram[0x8000] = hardware ? 0xea : 0x00;
        Mos6502 cpu(bus, CpuState{0x8000,0xfd,0,0,0,0x20});
        if (hardware) { cpu.set_irq(true); step(cpu); }
        const auto start = cpu.cycles();
        bus.on_cycle = [&](auto cycle) { if (cycle == start + (early ? 4 : 5)) cpu.set_nmi(true); };
        step(cpu);
        check(cpu.state().pc == (early ? 0xa000 : 0x9000), "NMI hijack cutoff/vector");
        check(bus.ram[0x1fc] == (hardware ? 1 : 2), "hijack changed pushed PC");
        check((bus.ram[0x1fb] & B) == (hardware ? 0 : B), "hijack changed pushed B");
        if (!early) {
            step(cpu);
            check(cpu.state().pc == 0x9001, "entry incorrectly polled interrupts");
            step(cpu);
            check(cpu.state().pc == 0xa000, "late NMI was not serviced after handler instruction");
        }
    }
}

void unsupported_and_failures() {
    TestBus variant_bus;
    bool invalid_variant = false;
    try { Mos6502 invalid(variant_bus, static_cast<Variant>(99)); }
    catch (const std::invalid_argument&) { invalid_variant = true; }
    check(invalid_variant && variant_bus.trace.empty(), "invalid variant must fail before bus access");
    // Independent documented-opcode list; all other encodings must fail.
    std::istringstream list(
        "00 01 05 06 08 09 0a 0d 0e 10 11 15 16 18 19 1d 1e "
        "20 21 24 25 26 28 29 2a 2c 2d 2e 30 31 35 36 38 39 3d 3e "
        "40 41 45 46 48 49 4a 4c 4d 4e 50 51 55 56 58 59 5d 5e "
        "60 61 65 66 68 69 6a 6c 6d 6e 70 71 75 76 78 79 7d 7e "
        "81 84 85 86 88 8a 8c 8d 8e 90 91 94 95 96 98 99 9a 9d "
        "a0 a1 a2 a4 a5 a6 a8 a9 aa ac ad ae b0 b1 b4 b5 b6 b8 b9 ba bc bd be "
        "c0 c1 c4 c5 c6 c8 c9 ca cc cd ce d0 d1 d5 d6 d8 d9 dd de "
        "e0 e1 e4 e5 e6 e8 e9 ea ec ed ee f0 f1 f5 f6 f8 f9 fd fe");
    std::set<unsigned> documented;
    unsigned code;
    while (list >> std::hex >> code) documented.insert(code);
    check(documented.size() == 151, "documented opcode count");
    for (code = 0; code < 256; ++code) if (!documented.contains(code)) {
        TestBus bus;
        setup(bus); bus.ram[0x8000] = static_cast<std::uint8_t>(code);
        Mos6502 cpu(bus, CpuState{0x8000,0xfd,0,0,0,0x24});
        bool rejected = false;
        try { cpu.clock(); }
        catch (const rse::cpu::UnsupportedOpcode& error) {
            rejected = true;
            check(error.opcode == code && error.address == 0x8000, "unsupported opcode diagnostic");
        }
        check(rejected && cpu.cycles() == 1 && bus.trace.size() == 1, "unsupported fetch behavior");
        bool faulted = false;
        try { cpu.clock(); } catch (const std::logic_error&) { faulted = true; }
        check(faulted && bus.trace.size() == 1, "faulted CPU dispatched bus access");
        cpu.reset(); step(cpu);
        check(cpu.state().pc == 0x8000 && cpu.cycles() == 8, "unsupported opcode reset recovery");
    }
    TestBus bus;
    setup(bus);
    Mos6502 cpu(bus, CpuState{0x8000,0xfd,0,0,0,0x24});
    bus.on_cycle = [](auto) { throw std::runtime_error("bus failure"); };
    bool failed = false;
    try { cpu.clock(); } catch (const std::runtime_error& error) { failed = std::string(error.what()) == "bus failure"; }
    check(failed && cpu.cycles() == 0, "bus failure propagation/completed cycle count");
    bus.on_cycle = {};
    cpu.reset(); step(cpu);
    check(cpu.state().pc == 0x8000 && cpu.cycles() == 7, "bus failure reset recovery");
}

void arithmetic() {
    TestBus bus;
    bus.recording = false;
    for (auto variant : {Variant::nmos6502, Variant::ricoh2a03}) {
        for (bool decimal : {false, true}) {
            if (variant == Variant::nmos6502 && decimal) continue;
            for (bool subtract : {false, true}) for (unsigned a = 0; a < 256; ++a)
                for (unsigned operand = 0; operand < 256; ++operand) for (unsigned carry = 0; carry < 2; ++carry) {
                    bus.ram[0x200] = subtract ? 0xe9 : 0x69;
                    bus.ram[0x201] = static_cast<std::uint8_t>(operand);
                    const int result = subtract ? int(a) - int(operand) - int(1 - carry) : int(a + operand + carry);
                    const auto byte = static_cast<std::uint8_t>(result);
                    const int signed_a = a < 128 ? int(a) : int(a) - 256;
                    const int signed_operand = operand < 128 ? int(operand) : int(operand) - 256;
                    const int signed_result = subtract ? signed_a - signed_operand - int(1 - carry) : signed_a + signed_operand + int(carry);
                    const auto p = static_cast<std::uint8_t>(U | I | (decimal ? D : 0) |
                        ((subtract ? result >= 0 : result > 255) ? C : 0) |
                        (byte == 0 ? Z : 0) | (byte & N) | ((signed_result < -128 || signed_result > 127) ? V : 0));
                    Mos6502 cpu(bus, CpuState{0x200,0xfd,static_cast<std::uint8_t>(a),0,0,
                        static_cast<std::uint8_t>(U | I | N | V | Z | carry | (decimal ? D : 0))}, variant);
                    step(cpu);
                    check(cpu.state() == CpuState{0x202,0xfd,byte,0,0,p} && cpu.cycles() == 2,
                          "exhaustive binary ADC/SBC state mismatch");
                }
        }
    }
    auto bcd = [](int value) { return static_cast<std::uint8_t>((value / 10) * 16 + value % 10); };
    for (bool subtract : {false, true}) for (int a = 0; a < 100; ++a)
        for (int operand = 0; operand < 100; ++operand) for (int carry = 0; carry < 2; ++carry) {
            bus.ram[0x200] = subtract ? 0xe9 : 0x69;
            bus.ram[0x201] = bcd(operand);
            Mos6502 cpu(bus, CpuState{0x200,0xfd,bcd(a),0,0,static_cast<std::uint8_t>(U | I | D | carry)});
            step(cpu);
            const int result = subtract ? a - operand - (1 - carry) : a + operand + carry;
            const bool expected_carry = subtract ? result >= 0 : result >= 100;
            check(cpu.state().a == bcd((result + 100) % 100) && bool(cpu.state().p & C) == expected_carry &&
                  (cpu.state().p & D), "exhaustive valid BCD ADC/SBC result/carry");
        }
}

void nrom_program() {
    std::vector<std::uint8_t> image(16 + 16384, 0);
    image[0] = 'N'; image[1] = 'E'; image[2] = 'S'; image[3] = 0x1a; image[4] = 1;
    std::fill(image.begin() + 16, image.end(), 0xea);
    const std::vector<std::uint8_t> program{0xa2,3,0xca,0xd0,0xfd,0xf8,0xa9,9,0x69,1,0x85,0x10};
    std::copy(program.begin(), program.end(), image.begin() + 16);
    image[16 + 0x3ffc] = 0; image[16 + 0x3ffd] = 0x80;
    rse::nes::Bus bus(rse::nes::Cartridge::parse(image));
    rse::nes::Cpu cpu(bus);
    step(cpu); // reset
    for (unsigned instruction = 0; instruction < 11; ++instruction) step(cpu);
    check(cpu.state().pc == 0x800c && cpu.state().x == 0 && cpu.state().a == 10 &&
          bus.cpu_read(0x10) == 10 && cpu.cycles() == 32, "NROM program or NES variant selection");
}

void boundary_cases() {
    {
        TestBus bus;
        bus.ram[0xffff] = 0xa9; bus.ram[0] = 0x80;
        Mos6502 cpu(bus, CpuState{0xffff,0xfd,0,0,0,0x24});
        step(cpu);
        trace(bus, {{0xffff,0xa9,false,1}, {0,0x80,false,2}});
        check(cpu.state().pc == 1 && cpu.state().a == 0x80, "PC operand wrap");
    }
    {
        TestBus bus;
        bus.ram[0x8000] = 0x6c; bus.ram[0x8001] = 0xff; bus.ram[0x8002] = 0x12;
        bus.ram[0x12ff] = 0x34; bus.ram[0x1200] = 0x56; bus.ram[0x1300] = 0x99;
        Mos6502 cpu(bus, CpuState{0x8000,0xfd,0,0,0,0x24});
        step(cpu);
        trace(bus, {{0x8000,0x6c,false,1}, {0x8001,0xff,false,2}, {0x8002,0x12,false,3},
                    {0x12ff,0x34,false,4}, {0x1200,0x56,false,5}});
        check(cpu.state().pc == 0x5634, "NMOS indirect JMP page wrap");
    }
    {
        TestBus bus;
        bus.ram[0x8000] = 0xb1; bus.ram[0x8001] = 0xff;
        bus.ram[0xff] = 0xff; bus.ram[0] = 0x12;
        bus.ram[0x1200] = 0x11; bus.ram[0x1300] = 0x42;
        Mos6502 cpu(bus, CpuState{0x8000,0xfd,0,0,1,0x24});
        step(cpu);
        trace(bus, {{0x8000,0xb1,false,1}, {0x8001,0xff,false,2}, {0xff,0xff,false,3},
                    {0,0x12,false,4}, {0x1200,0x11,false,5}, {0x1300,0x42,false,6}});
        check(cpu.state().a == 0x42, "zero-page pointer wrap or indexed crossing");
    }
    {
        TestBus bus;
        bus.ram[0x8000] = 0x9d; bus.ram[0x8001] = 0xff; bus.ram[0x8002] = 0xff;
        bus.ram[0xff00] = 0x11;
        Mos6502 cpu(bus, CpuState{0x8000,0xfd,0x42,1,0,0x24});
        step(cpu);
        trace(bus, {{0x8000,0x9d,false,1}, {0x8001,0xff,false,2}, {0x8002,0xff,false,3},
                    {0xff00,0x11,false,4}, {0,0x42,true,5}});
    }
    {
        TestBus bus;
        bus.ram[0x8000] = 0x1e; bus.ram[0x8001] = 0xff; bus.ram[0x8002] = 0x12;
        bus.ram[0x1200] = 0x55; bus.ram[0x1300] = 0x81;
        Mos6502 cpu(bus, CpuState{0x8000,0xfd,0,1,0,0x24});
        step(cpu);
        trace(bus, {{0x8000,0x1e,false,1}, {0x8001,0xff,false,2}, {0x8002,0x12,false,3},
                    {0x1200,0x55,false,4}, {0x1300,0x81,false,5},
                    {0x1300,0x81,true,6}, {0x1300,2,true,7}});
        check(cpu.state().p == 0x25, "RMW flags");
    }
    {
        TestBus bus;
        bus.ram[0x8000] = 0x48; bus.ram[0x8001] = 0x68;
        Mos6502 cpu(bus, CpuState{0x8000,0,0x42,0,0,0x24});
        step(cpu);
        check(cpu.state().s == 0xff && bus.ram[0x100] == 0x42, "stack push wrap");
        step(cpu);
        check(cpu.state().s == 0 && cpu.state().a == 0x42 && cpu.cycles() == 7, "stack pull wrap");
    }
    {
        TestBus bus;
        bus.ram[0] = 0xd0; bus.ram[1] = 0xfd;
        Mos6502 cpu(bus, CpuState{0,0xfd,0,0,0,0x24});
        step(cpu);
        trace(bus, {{0,0xd0,false,1}, {1,0xfd,false,2}, {2,0,false,3}, {0xff,0,false,4}});
        check(cpu.state().pc == 0xffff, "negative branch PC wrap");
    }
}
}

int main() {
    try {
        reset_cases(); irq_and_nmi(); interrupt_latency(); branch_polling();
        hijacking(); unsupported_and_failures(); boundary_cases(); arithmetic(); nrom_program();
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
