// SPDX-License-Identifier: Apache-2.0
#include "cpu_test_bus.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

namespace {
using rse::cpu::Mos6502;
using rse::cpu::CpuState;
unsigned number(std::istream& input, unsigned limit) {
    unsigned value{};
    check(bool(input >> value) && value <= limit, "malformed reference data");
    return value;
}
using Memory = std::vector<std::pair<std::uint16_t, std::uint8_t>>;
CpuState read_state(std::istream& input, Memory& memory) {
    CpuState state;
    state.pc = static_cast<std::uint16_t>(number(input, 65535));
    state.s = static_cast<std::uint8_t>(number(input, 255));
    state.a = static_cast<std::uint8_t>(number(input, 255));
    state.x = static_cast<std::uint8_t>(number(input, 255));
    state.y = static_cast<std::uint8_t>(number(input, 255));
    state.p = static_cast<std::uint8_t>(number(input, 255));
    const auto count = number(input, 32);
    for (unsigned i = 0; i < count; ++i) {
        const auto address = static_cast<std::uint16_t>(number(input, 65535));
        const auto value = static_cast<std::uint8_t>(number(input, 255));
        memory.emplace_back(address, value);
    }
    return state;
}
std::string describe(const CpuState& r) {
    std::ostringstream text;
    text << "PC=" << r.pc << " S=" << unsigned(r.s) << " A=" << unsigned(r.a)
         << " X=" << unsigned(r.x) << " Y=" << unsigned(r.y) << " P=" << unsigned(r.p);
    return text.str();
}
std::string describe(const Access& a) {
    std::ostringstream text;
    text << (a.writing ? "write " : "read ") << a.address << "=" << unsigned(a.value)
         << " at cycle " << a.cycle;
    return text.str();
}
void references(const char* path, rse::cpu::Variant variant) {
    std::ifstream input(path);
    check(bool(input), "could not open pinned CPU vectors");
    std::string format;
    input >> format;
    check(format == "RSE_NES6502_V1", "unknown CPU vector format");
    const auto count = number(input, 1510000);
    check(count == 151 * 64, "unexpected pinned vector count");
    std::array<unsigned, 256> coverage{};
    for (unsigned index = 0; index < count; ++index) {
        std::string name;
        check(bool(input >> name), "missing reference name");
        try {
            Memory initial_ram, final_ram;
            const auto initial = read_state(input, initial_ram);
            const auto expected_state = read_state(input, final_ram);
            const auto cycle_count = number(input, 7);
            check(cycle_count >= 2, "reference cycle count too small");
            std::vector<Access> expected;
            for (unsigned i = 0; i < cycle_count; ++i) {
                const auto address = static_cast<std::uint16_t>(number(input, 65535));
                const auto value = static_cast<std::uint8_t>(number(input, 255));
                const bool writing = number(input, 1) != 0;
                expected.push_back({address, value, writing, i + 1});
            }
            TestBus bus;
            for (auto [address, value] : initial_ram) bus.ram[address] = value;
            ++coverage[bus.ram[initial.pc]];
            Mos6502 cpu(bus, initial, variant);
            for (unsigned i = 0; i < cycle_count; ++i) {
                check(i == 0 || !cpu.at_instruction_boundary(), "instruction completed early");
                cpu.clock();
                check(bus.trace.size() == i + 1, "clock did not perform exactly one bus access");
                check(bus.trace.back() == expected[i],
                      "expected " + describe(expected[i]) + ", got " + describe(bus.trace.back()));
            }
            check(cpu.at_instruction_boundary(), "instruction did not finish at expected cycle");
            check(cpu.cycles() == cycle_count, "CPU cycle count mismatch");
            check(cpu.state() == expected_state,
                  "expected " + describe(expected_state) + ", got " + describe(cpu.state()));
            for (auto [address, value] : final_ram)
                check(bus.ram[address] == value, "final RAM mismatch at " + std::to_string(address));
        } catch (const std::exception& error) {
            throw std::runtime_error(name + ": " + error.what());
        }
    }
    unsigned opcodes = 0;
    for (auto cases : coverage) if (cases) {
        ++opcodes;
        check(cases == 64, "opcode reference coverage mismatch");
    }
    check(opcodes == 151, "not all documented opcodes covered");
    std::string extra;
    check(!(input >> extra), "trailing reference data");
    std::cout << count << " reference cases passed for " << opcodes << " opcodes\n";
}
}
int main(int argc, char** argv) {
    try {
        check(argc == 3, "expected variant and fixture path");
        check(std::string(argv[1]) == "nmos" || std::string(argv[1]) == "2a03", "unknown variant");
        references(argv[2], std::string(argv[1]) == "nmos" ? rse::cpu::Variant::nmos6502 : rse::cpu::Variant::ricoh2a03);
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
