// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>

namespace rse::chips {
struct Cd4021State {
    std::uint8_t parallel{}, shift{};
    bool load{}, serial{};
    bool operator==(const Cd4021State&) const = default;
};
// Logical CD4021 8-stage parallel-in/serial-out register: Q8 is bit 7.
// Load is asynchronous/high-active; clock() represents one rising edge.
// No propagation-delay/electrical model. Wiring/bit order belong to the board.
class Cd4021 {
public:
    void set_parallel(std::uint8_t value) noexcept {
        state_.parallel = value;
        if (state_.load) state_.shift = value;
    }
    void set_load(bool high) noexcept {
        state_.load = high;
        if (high) state_.shift = state_.parallel;
    }
    void set_serial(bool high) noexcept { state_.serial = high; }
    void clock() noexcept {
        if (state_.load) state_.shift = state_.parallel;
        else state_.shift = static_cast<std::uint8_t>((state_.shift << 1) | unsigned(state_.serial));
    }
    [[nodiscard]] bool output() const noexcept { return (state_.shift & 0x80) != 0; }
    [[nodiscard]] Cd4021State state() const noexcept { return state_; }
    void restore(Cd4021State state) noexcept {state_=state;}
private:
    Cd4021State state_;
};
} // namespace rse::chips
