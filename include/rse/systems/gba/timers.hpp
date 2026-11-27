// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <array>
#include <cstdint>
#include <limits>

namespace rse::gba {

// Four 16-bit reloadable counters. The board advances to next_overflow()
// before reading or writing timer registers at a given tick.
class Timers {
public:
    static constexpr auto never = std::numeric_limits<std::uint64_t>::max();
    std::uint16_t read(unsigned offset, std::uint64_t tick) const noexcept;
    void write(unsigned offset, std::uint16_t value, std::uint16_t mask, std::uint64_t tick) noexcept;
    std::uint64_t next_overflow() const noexcept { return next_; }
    // A bit per timer that overflowed, including count-up timers.
    unsigned overflow(std::uint64_t tick) noexcept;
    bool irq_enabled(unsigned index) const noexcept { return counters_[index].control & 0x40; }

private:
    struct Counter {
        std::uint16_t reload{}, value{}, control{};
        std::uint64_t origin{}, next{never};
        bool running() const noexcept { return control & 0x80; }
        bool cascades() const noexcept { return control & 4; }
        unsigned period() const noexcept;
        std::uint16_t read(std::uint64_t tick) const noexcept;
        void reschedule(std::uint64_t tick) noexcept;
    };
    void select_next() noexcept;
    std::array<Counter, 4> counters_{};
    std::uint64_t next_{never};
};

}
