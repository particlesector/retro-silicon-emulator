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

enum class Operation { arm, thumb, store };
struct Access {
    Operation operation;
    std::uint32_t address;
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
        trace.push_back({Operation::arm, address});
        return read(address, 4);
    }
    std::uint16_t fetch_thumb(std::uint32_t address) override {
        trace.push_back({Operation::thumb, address});
        return static_cast<std::uint16_t>(read(address, 2));
    }
    void store_word(std::uint32_t address, std::uint32_t value) override {
        trace.push_back({Operation::store, address});
        put(address, value, 4);
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
        {Operation::arm, 0}, {Operation::arm, 4}, {Operation::arm, 8},
        {Operation::arm, 12}, {Operation::thumb, 0x22}, {Operation::thumb, 0x24},
        {Operation::thumb, 0x26}, {Operation::store, 0x84},
        {Operation::thumb, 0x28}, {Operation::arm, 0x40}
    };
    check(memory.trace == expected, "instruction fetch and data access order");
    cpu.reset();
    check(cpu.state().pc == 0 && cpu.state().cpsr == 0xd3 &&
          cpu.state().registers == Arm7State{}.registers, "deterministic reset");
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
    memory.put(0, 0xe3b00001, 4); // ARM MOVS is outside the initial subset.
    reject("Unsupported ARM instruction 0xe3b00001 at 0x0");
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
        unsupported_encodings();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
