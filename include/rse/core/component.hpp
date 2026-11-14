// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
namespace rse {
// Integer simulated time in system-defined master-clock units, never host time.
using Tick = std::uint64_t;
class Component {
public:
    virtual ~Component() = default;
    // Restore deterministic state at tick zero. May be called after a failure.
    virtual void reset() = 0;
    // Process events through target (inclusive), exactly once. Machine calls
    // this at consecutive ticks beginning at 1; reset establishes tick 0.
    // Implementations must not reenter or mutate their owning Machine.
    virtual void advance_to(Tick target) = 0;
};
} // namespace rse
