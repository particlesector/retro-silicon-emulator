// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "bus.hpp"

namespace rse::gba {

// Instruction-boundary headless machine. A step advances a CPU instruction
// or IRQ entry, one DMA transfer unit, or an idle span while halted.
class Machine {
public:
    Machine(std::span<const std::uint8_t> bios, std::span<const std::uint8_t> cartridge, bool capture_audio = false)
        : bus_(bios, cartridge, capture_audio), cpu_(bus_) {}
    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;
    void reset() noexcept;
    void step();
    // Advance every device exactly through the requested board tick.
    void advance_to(std::uint64_t tick);
    void schedule_keys(std::uint64_t tick, std::uint16_t pressed) { bus_.schedule_keys(tick, pressed); }
    [[nodiscard]] std::vector<std::int16_t> take_audio() { return bus_.take_audio(); }
    [[nodiscard]] const Bus& bus() const noexcept { return bus_; }
    [[nodiscard]] const cpu::Arm7State& cpu() const noexcept { return cpu_.state(); }

private:
    void step_until(std::uint64_t limit);
    Bus bus_;
    cpu::Arm7Tdmi cpu_;
    bool cpu_in_progress_{};
};

}
