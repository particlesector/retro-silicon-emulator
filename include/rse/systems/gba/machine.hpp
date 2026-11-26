// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "bus.hpp"

namespace rse::gba {

// Instruction-boundary headless machine. A step advances a CPU instruction
// or IRQ entry, one DMA transfer unit, or an idle span while halted.
class Machine {
public:
    Machine(std::span<const std::uint8_t> bios, std::span<const std::uint8_t> cartridge)
        : bus_(bios, cartridge), cpu_(bus_) {}
    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;
    void reset() noexcept;
    void step();
    [[nodiscard]] const Bus& bus() const noexcept { return bus_; }
    [[nodiscard]] const cpu::Arm7State& cpu() const noexcept { return cpu_.state(); }

private:
    Bus bus_;
    cpu::Arm7Tdmi cpu_;
};

}
