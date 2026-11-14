// SPDX-License-Identifier: Apache-2.0
#include <rse/core/machine.hpp>
#include <stdexcept>
#include <utility>
namespace rse {
void Machine::add(std::unique_ptr<Component> component) {
    if (!component) throw std::invalid_argument("null component");
    if (started_ || faulted_) throw std::logic_error("registration is closed");
    component->reset();
    components_.push_back(std::move(component));
}
void Machine::reset() {
    faulted_ = true;
    for (auto& component : components_) component->reset();
    tick_ = 0;
    faulted_ = false;
}
void Machine::advance_to(Tick target) {
    if (faulted_) throw std::logic_error("machine requires reset after failure");
    if (target < tick_) throw std::invalid_argument("cannot rewind time");
    if (target == tick_) return;
    started_ = true;
    try {
        while (tick_ < target) {
            const Tick next = tick_ + 1;
            for (auto& component : components_) component->advance_to(next);
            tick_ = next;
        }
    } catch (...) {
        faulted_ = true;
        throw;
    }
}
}
