// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <rse/core/component.hpp>
#include <memory>
#include <vector>
namespace rse {
class Machine {
public:
    // Take ownership and reset the component before registration. Reject null
    // or registration after execution starts (even following reset).
    void add(std::unique_ptr<Component> component);
    // Reset in registration order. Success clears a fault and sets tick to 0.
    void reset();
    // Advance each tick in registration order. Equal targets are no-ops;
    // rewinds are invalid. Component exceptions fault the machine until reset.
    void advance_to(Tick target);
    // Last fully completed tick; component state can be partial after a fault.
    [[nodiscard]] Tick tick() const noexcept { return tick_; }
private:
    Tick tick_{};
    bool started_{};
    bool faulted_{};
    std::vector<std::unique_ptr<Component>> components_;
};
}
