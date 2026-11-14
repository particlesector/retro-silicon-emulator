// SPDX-License-Identifier: Apache-2.0
#include <rse/core/machine.hpp>
#include <iostream>
#include <memory>
#include <stdexcept>
struct Counter final : rse::Component {
    rse::Tick now{};
    void reset() override { now = 0; }
    void advance_to(rse::Tick t) override { now = t; }
};
int main() {
    rse::Machine machine;
    auto component = std::make_unique<Counter>();
    auto* observer = component.get();
    machine.add(std::move(component));
    machine.advance_to(42);
    if (machine.tick() != 42 || observer->now != 42) {
        std::cerr << "advance_to must update machine and component ticks\n";
        return 1;
    }
    machine.reset();
    if (machine.tick() != 0 || observer->now != 0) {
        std::cerr << "reset must clear machine and component ticks\n";
        return 1;
    }
    bool threw = false;
    try { machine.advance_to(10); machine.advance_to(9); }
    catch (const std::invalid_argument&) { threw = true; }
    if (!threw) {
        std::cerr << "advance_to must reject rewinding time\n";
        return 1;
    }
    return 0;
}
