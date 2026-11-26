// SPDX-License-Identifier: Apache-2.0
#include <rse/cpu/arm7tdmi.hpp>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using namespace rse::cpu;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

enum class Operation { arm, thumb, store, load, load_byte, load_half, store_byte, store_half };
struct Access {
    Operation operation;
    std::uint32_t address;
    std::uint32_t value;
    bool operator==(const Access&) const = default;
};

struct Memory : Arm7Bus {
    std::array<std::uint8_t, 256> bytes{};
    std::vector<Access> trace;

    void put(std::uint32_t address, std::uint32_t value, unsigned width) {
        for (unsigned i = 0; i < width; ++i)
            bytes.at(address + i) = static_cast<std::uint8_t>(value >> (8 * i));
    }
    std::uint32_t read(std::uint32_t address, unsigned width) const {
        std::uint32_t value = 0;
        for (unsigned i = 0; i < width; ++i)
            value |= std::uint32_t{bytes.at(address + i)} << (8 * i);
        return value;
    }
    std::uint32_t fetch_arm(std::uint32_t address) override {
        const auto value = read(address, 4);
        trace.push_back({Operation::arm, address, value});
        return value;
    }
    std::uint16_t fetch_thumb(std::uint32_t address) override {
        const auto value = static_cast<std::uint16_t>(read(address, 2));
        trace.push_back({Operation::thumb, address, value});
        return value;
    }
    void store_word(std::uint32_t address, std::uint32_t value) override {
        trace.push_back({Operation::store, address, value});
        put(address, value, 4);
    }
    std::uint32_t load_word(std::uint32_t address) override {
        const auto value = read(address, 4);
        trace.push_back({Operation::load, address, value});
        return value;
    }
    std::uint16_t load_half(std::uint32_t address) override {
        const auto value = static_cast<std::uint16_t>(read(address, 2));
        trace.push_back({Operation::load_half, address, value});
        return value;
    }
    std::uint8_t load_byte(std::uint32_t address) override {
        const auto value = bytes.at(address);
        trace.push_back({Operation::load_byte, address, value});
        return value;
    }
    void store_half(std::uint32_t address, std::uint16_t value) override {
        trace.push_back({Operation::store_half, address, value});
        put(address, value, 2);
    }
    void store_byte(std::uint32_t address, std::uint8_t value) override {
        trace.push_back({Operation::store_byte, address, value});
        put(address, value, 1);
    }
};

void interworking() {
    Memory memory;
    memory.bytes.fill(0xcc);
    memory.put(0x00, 0xe3a00023, 4); // MOV r0, #0x23  (Thumb entry at 0x22)
    memory.put(0x04, 0xe3a01040, 4); // MOV r1, #0x40  (ARM return address)
    memory.put(0x08, 0xe3a02080, 4); // MOV r2, #0x80
    memory.put(0x0c, 0xe12fff10, 4); // BX r0
    memory.put(0x22, 0x2300, 2);     // MOVS r3, #0
    memory.put(0x24, 0x235a, 2);     // MOVS r3, #0x5a
    memory.put(0x26, 0x6053, 2);     // STR r3, [r2, #4]
    memory.put(0x28, 0x4708, 2);     // BX r1
    memory.put(0x40, 0xe3a04102, 4); // MOV r4, #0x80000000
    Arm7Tdmi cpu(memory);
    for (int i = 0; i < 4; ++i) cpu.step();
    check(cpu.state().pc == 0x22 && cpu.state().cpsr == 0xf3, "ARM to Thumb");
    cpu.step();
    check(cpu.state().cpsr == 0x400000f3u, "MOVS zero");
    cpu.step();
    check(cpu.state().cpsr == 0xf3, "MOVS nonzero");
    cpu.step();
    cpu.step();
    check(cpu.state().pc == 0x40 && cpu.state().cpsr == 0xd3, "Thumb to ARM");
    cpu.step();
    check(cpu.state().pc == 0x44 && cpu.state().registers[4] == 0x80000000u,
          "final PC and rotated immediate");
    check(memory.read(0x84, 4) == 0x5a && memory.bytes[0x83] == 0xcc &&
          memory.bytes[0x88] == 0xcc, "RAM signature and adjacent bytes");
    const std::vector<Access> expected = {
        {Operation::arm, 0, 0xe3a00023}, {Operation::arm, 4, 0xe3a01040},
        {Operation::arm, 8, 0xe3a02080}, {Operation::arm, 12, 0xe12fff10},
        {Operation::thumb, 0x22, 0x2300}, {Operation::thumb, 0x24, 0x235a},
        {Operation::thumb, 0x26, 0x6053}, {Operation::store, 0x84, 0x5a},
        {Operation::thumb, 0x28, 0x4708}, {Operation::arm, 0x40, 0xe3a04102}
    };
    check(memory.trace == expected, "instruction fetch and data access order");
    cpu.reset();
    check(cpu.state().pc == 0 && cpu.state().cpsr == 0xd3 &&
          cpu.state().registers == Arm7State{}.registers, "deterministic reset");
}


void arm_flags_and_branches() {
    Memory memory;
    const std::uint32_t code[] = {
        0xe3a00102, // MOV  r0,#0x80000000
        0xe2501001, // SUBS r1,r0,#1
        0xe2912001, // ADDS r2,r1,#1
        0xe1b03080, // MOVS r3,r0,LSL #1
        0xe2a34000, // ADC  r4,r3,#0
        0xe1a0500f, // MOV  r5,pc
        0x1a000001, // BNE  0x24 (skipped)
        0x0b000000, // BLEQ 0x24
        0xe3a060ff, // skipped
        0xe3a06033  // MOV  r6,#0x33
    };
    for (unsigned i = 0; i < std::size(code); ++i) memory.put(4*i, code[i], 4);
    Arm7Tdmi cpu(memory);
    cpu.run(2);
    check(cpu.state().registers[1] == 0x7fffffffu && cpu.state().cpsr == 0x300000d3,
          "ARM signed subtraction overflow with no borrow");
    cpu.step();
    check(cpu.state().registers[2] == 0x80000000u && cpu.state().cpsr == 0x900000d3,
          "ARM signed addition overflow");
    cpu.step();
    check(cpu.state().registers[3] == 0 && cpu.state().cpsr == 0x700000d3,
          "ARM left shift carry and preserved overflow");
    cpu.run(3);
    check(cpu.state().registers[4] == 1 && cpu.state().registers[5] == 0x1c &&
          cpu.state().pc == 0x1c, "ADC, pipeline-visible PC and untaken branch");
    cpu.run(2);
    check(cpu.state().pc == 0x28 && cpu.state().registers[14] == 0x20 &&
          cpu.state().registers[6] == 0x33, "taken conditional branch and link");
}

void thumb_flags_and_branches() {
    Memory memory;
    memory.put(0, 0xe3a00021, 4);
    memory.put(4, 0xe12fff10, 4);
    const std::uint16_t code[] = {
        0x2101, // MOVS r1,#1
        0x07c9, // LSLS r1,r1,#31
        0x004a, // LSLS r2,r1,#1
        0x3a01, // SUBS r2,#1
        0x3201, // ADDS r2,#1
        0xd101, // BNE forward (skipped)
        0x467b, // MOV r3,pc at 0x2c
        0x2402, // MOVS r4,#2
        0x3c01, // SUBS r4,#1
        0xd1fd  // BNE 0x30
    };
    for (unsigned i = 0; i < std::size(code); ++i) memory.put(0x20+2*i, code[i], 2);
    Arm7Tdmi cpu(memory);
    cpu.run(5);
    check(cpu.state().registers[1] == 0x80000000u && cpu.state().registers[2] == 0 &&
          cpu.state().cpsr == 0x600000f3, "Thumb shift carry and zero");
    cpu.step();
    check(cpu.state().registers[2] == 0xffffffff && cpu.state().cpsr == 0x800000f3,
          "Thumb borrow");
    cpu.step();
    check(cpu.state().registers[2] == 0 && cpu.state().cpsr == 0x600000f3, "Thumb add carry");
    cpu.run(5);
    check(cpu.state().pc == 0x30 && cpu.state().registers[4] == 1 &&
          cpu.state().registers[3] == 0x30, "Thumb visible PC and backward branch");
    cpu.run(2);
    check(cpu.state().pc == 0x34 && cpu.state().registers[4] == 0, "Thumb loop exit");
}

void register_shift_edges() {
    struct Case { unsigned kind, count; std::uint32_t value; bool carry; };
    constexpr Case cases[] = {
        {0,0,0x80000001,false}, {0,31,0x80000000,false}, {0,32,0,true}, {0,33,0,false},
        {1,31,1,false}, {1,32,0,true}, {1,255,0,false}, {2,33,0xffffffff,true},
        {3,0,0x80000001,false}, {3,32,0x80000001,true}, {3,33,0xc0000000,true}
    };
    for (auto sample : cases) {
        Memory memory;
        memory.put(0, 0xe3a00102, 4); // r0 = 0x80000000
        memory.put(4, 0xe3800001, 4); // ORR r0,r0,#1
        memory.put(8, 0xe3a01000 | sample.count, 4);
        memory.put(12, 0xe1b03110 | (sample.kind << 5), 4); // MOVS r3,r0,shift r1
        Arm7Tdmi cpu(memory);
        cpu.run(4);
        check(cpu.state().registers[3] == sample.value &&
              bool(cpu.state().cpsr & (1u << 29)) == sample.carry, "register shift edge");
    }
}

void transfer_widths(bool thumb) {
    Memory memory;
    memory.put(0xa0, 0x80604020, 4);
    memory.put(0, 0xe3a000a1, 4); // Deliberately unaligned base for the first LDR.
    memory.put(4, 0xe3a010c3, 4);
    if (thumb) {
        memory.put(8, 0xe3a07021, 4);
        memory.put(12, 0xe12fff17, 4);
        const std::uint16_t code[] = {
            0x6802, 0x20a0, 0x7041, 0x7843, 0x8042, 0x8844, 0x20a5, 0x6002, 0x6805
        };
        for (unsigned i=0; i<std::size(code); ++i) memory.put(0x20+2*i, code[i], 2);
    } else {
        const std::uint32_t code[] = {
            0xe5902000, 0xe3a000a0, 0xe5c01001, 0xe5d03001, 0xe1c020b2,
            0xe1d040b2, 0xe3a000a5, 0xe5802000, 0xe5905000
        };
        for (unsigned i=0; i<std::size(code); ++i) memory.put(8+4*i, code[i], 4);
    }
    Arm7Tdmi cpu(memory);
    cpu.run(thumb ? 13 : 11);
    const auto& r = cpu.state().registers;
    check(r[2] == 0x20806040 && r[3] == 0xc3 && r[4] == 0x6040 && r[5] == 0x40208060,
          "rotated word, byte and halfword loads");
    check(memory.read(0xa0,4) == 0x6040c320 && memory.read(0xa4,4) == 0x20806040,
          "lane stores and aligned word store");
    std::vector<Access> data;
    for (const auto& a : memory.trace)
        if (a.operation != Operation::arm && a.operation != Operation::thumb) data.push_back(a);
    const std::vector<Access> expected = {
        {Operation::load,0xa0,0x80604020}, {Operation::store_byte,0xa1,0xc3},
        {Operation::load_byte,0xa1,0xc3}, {Operation::store_half,0xa2,0x6040},
        {Operation::load_half,0xa2,0x6040}, {Operation::store,0xa4,0x20806040},
        {Operation::load,0xa4,0x20806040}
    };
    check(data == expected, "physical data widths, addresses and values");
}

Memory exception_program() {
    Memory memory;
    memory.put(0x00, 0xea000006, 4); // B 0x20 (reset)
    memory.put(0x08, 0xea000014, 4); // B 0x60 (SWI)
    memory.put(0x18, 0xea000016, 4); // B 0x78 (IRQ)
    memory.put(0x20, 0xe3a0d0b0, 4); // SVC SP = 0xb0
    memory.put(0x24, 0xe3a0e0aa, 4); // SVC LR = 0xaa
    memory.put(0x28, 0xe321f09f, 4); // MSR CPSR_c,#0x9f (System, IRQ masked)
    memory.put(0x2c, 0xe3a0d0a0, 4); // System SP = 0xa0
    memory.put(0x30, 0xe3a0e0bc, 4); // System LR = 0xbc
    memory.put(0x34, 0xe3a00041, 4);
    memory.put(0x38, 0xe12fff10, 4); // BX to Thumb at 0x40
    memory.put(0x40, 0x2100, 2);     // MOVS r1,#0 (Z set)
    memory.put(0x42, 0xdf07, 2);     // SWI 7; next Thumb PC is 0x44
    memory.put(0x44, 0x2109, 2);     // Marker reached after IRQ return
    memory.put(0x60, 0xe14f4000, 4); // MRS r4,SPSR
    memory.put(0x64, 0xe582d0c0, 4); // STR sp,[r2,#0xc0]
    memory.put(0x68, 0xe361f03f, 4); // MSR SPSR_c,#0x3f (unmask IRQ on return)
    memory.put(0x6c, 0xe1b0f00e, 4); // MOVS pc,lr
    memory.put(0x78, 0xe14f5000, 4); // MRS r5,SPSR
    memory.put(0x7c, 0xe3a0d0d0, 4); // IRQ SP = 0xd0
    memory.put(0x80, 0xe25ef004, 4); // SUBS pc,lr,#4
    return memory;
}

void thumb_exception_boundaries() {
    auto memory = exception_program();
    Arm7Tdmi cpu(memory);
    const auto& state = cpu.state();
    cpu.set_irq(true);
    cpu.run(10);
    check(state.pc == 8 && state.cpsr == 0x40000093 && state.supervisor.spsr == 0x400000bf,
          "Thumb SWI vector, masks and saved status");
    check(state.registers[14] == 0x44 && state.registers[13] == 0xb0,
          "SWI link and banked SVC stack");
    cpu.run(5);
    check(state.pc == 0x44 && state.cpsr == 0x4000003f && state.registers[4] == 0x400000bf &&
          state.registers[13] == 0xa0 && state.registers[14] == 0xbc &&
          memory.read(0xc0, 4) == 0xb0, "SWI return to System Thumb");
    const auto accesses = memory.trace.size();
    cpu.step();
    check(state.pc == 0x18 && state.cpsr == 0x40000092 && state.irq.spsr == 0x4000003f &&
          state.registers[14] == 0x48 && state.registers[13] == 0 &&
          memory.trace.size() == accesses, "unmasked IRQ accepted before Thumb fetch");
    cpu.run(4);
    check(state.pc == 0x44 && state.cpsr == 0x4000003f && state.irq.sp == 0xd0 &&
          state.registers[5] == 0x4000003f && state.registers[13] == 0xa0 &&
          state.registers[14] == 0xbc, "IRQ return restores status, PC and System bank");

    auto other_memory = exception_program();
    Arm7Tdmi other(other_memory);
    other.set_irq(true);
    other.run(20);
    check(other.state() == state && other_memory.trace == memory.trace &&
          other_memory.bytes == memory.bytes, "exception fixture single-budget replay");
    other.reset();
    other.set_irq(true);
    other_memory = exception_program();
    for (unsigned count : {0, 1, 7, 1, 3, 2, 4, 2}) other.run(count);
    check(other.state() == state && other_memory.trace == memory.trace &&
          other_memory.bytes == memory.bytes, "uneven budgets and reset replay");

    cpu.step();
    check(state.pc == 0x18 && state.registers[13] == 0xd0, "held IRQ re-enters with IRQ stack");
    cpu.set_irq(false);
    cpu.run(5);
    check(state.pc == 0x46 && state.registers[1] == 9, "return to interrupted Thumb instruction");
}

void arm_exception_boundaries() {
    Memory memory;
    memory.put(0, 0xef00000a, 4);     // ARM SWI from supervisor
    memory.put(4, 0xe321f01f, 4);    // MSR CPSR_c,#0x1f
    memory.put(8, 0xe1b0f00e, 4);    // MOVS pc,lr
    memory.put(0x18, 0xe25ef004, 4); // SUBS pc,lr,#4
    Arm7Tdmi cpu(memory);
    cpu.step();
    check(cpu.state().pc == 8 && cpu.state().registers[14] == 4 &&
          cpu.state().supervisor.spsr == 0xd3, "ARM SWI entry");
    cpu.step();
    check(cpu.state().pc == 4 && cpu.state().cpsr == 0xd3, "ARM SWI return");
    cpu.set_irq(true);
    cpu.run(2);
    check(cpu.state().pc == 0x18 && cpu.state().registers[14] == 12 &&
          cpu.state().irq.spsr == 0x1f, "ARM IRQ link, vector and SPSR");
    cpu.set_irq(false);
    cpu.step();
    check(cpu.state().pc == 8 && cpu.state().cpsr == 0x1f, "ARM IRQ return");
}
void unsupported_encodings() {
    Memory memory;
    Arm7Tdmi cpu(memory);
    auto reject = [&](std::string_view expected) {
        auto before = cpu.state();
        try {
            cpu.step();
        } catch (const std::runtime_error& error) {
            check(error.what() == expected, "opcode diagnostic");
            check(cpu.state().pc == before.pc && cpu.state().cpsr == before.cpsr &&
                  cpu.state().registers == before.registers, "failed instruction committed");
            return;
        }
        throw std::runtime_error("unsupported instruction executed");
    };
    memory.put(0, 0xe16f0f10, 4); // CLZ requires ARMv5.
    reject("Unsupported ARM instruction 0xe16f0f10 at 0x0");
    memory.put(0, 0xe3a00009, 4);
    memory.put(4, 0xe12fff10, 4);
    memory.put(8, 0x4780, 2); // BLX register is unavailable on ARMv4T.
    cpu.step();
    cpu.step();
    reject("Unsupported Thumb instruction 0x4780 at 0x8");
}
}

int main() {
    try {
        interworking();
        arm_flags_and_branches();
        thumb_flags_and_branches();
        register_shift_edges();
        transfer_widths(false);
        transfer_widths(true);
        arm_exception_boundaries();
        thumb_exception_boundaries();
        unsupported_encodings();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
