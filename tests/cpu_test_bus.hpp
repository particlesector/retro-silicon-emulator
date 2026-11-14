// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <rse/cpu/mos6502.hpp>
#include <array>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

struct Access {
    std::uint16_t address;
    std::uint8_t value;
    bool writing;
    std::uint64_t cycle;
    bool operator==(const Access&) const = default;
};
struct TestBus final : rse::cpu::Bus {
    std::array<std::uint8_t, 65536> ram{};
    std::vector<Access> trace;
    std::function<void(std::uint64_t)> on_cycle;
    bool recording{true};
    std::uint8_t read(std::uint16_t address, std::uint64_t cycle) override {
        if (recording) trace.push_back({address, ram[address], false, cycle});
        if (on_cycle) on_cycle(cycle);
        return ram[address];
    }
    void write(std::uint16_t address, std::uint8_t value, std::uint64_t cycle) override {
        if (recording) trace.push_back({address, value, true, cycle});
        ram[address] = value;
        if (on_cycle) on_cycle(cycle);
    }
};
inline void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Cpu>
inline void step(Cpu& cpu) {
    unsigned cycles = 0;
    do {
        cpu.clock();
        check(++cycles <= 7, "instruction did not finish within seven cycles");
    } while (!cpu.at_instruction_boundary());
}
