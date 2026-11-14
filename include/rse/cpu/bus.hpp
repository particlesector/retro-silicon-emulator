// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>

namespace rse::cpu {
// One read/write per granted CPU clock. Cycle numbers are 1-based CPU
// cycles, not master-clock ticks. The bus must outlive its CPU. Callbacks may
// update interrupt pins but must not call clock() or reset() on the CPU.
class Bus {
public:
    virtual ~Bus() = default;
    // Called before access. An external bus owner may deny a read grant and
    // perform its own transaction. The CPU still clocks/samples interrupt pins,
    // but does not consume the pending read. Denying a write is an error.
    virtual bool acquire(std::uint16_t, bool, std::uint64_t) { return true; }
    virtual std::uint8_t read(std::uint16_t address, std::uint64_t cycle) = 0;
    virtual void write(std::uint16_t address, std::uint8_t value, std::uint64_t cycle) = 0;
};
} // namespace rse::cpu
