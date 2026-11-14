// SPDX-License-Identifier: Apache-2.0
#include <rse/core/machine.hpp>
#include <stdexcept>
#include <utility>
namespace rse {
void Machine::add(std::unique_ptr<Component> component) {
    if (!component) throw std::invalid_argument("null component");
    components_.push_back(std::move(component));
}
void Machine::reset() {
    tick_ = 0;
    for (auto& component : components_) component->reset();
}
void Machine::advance_to(Tick target) {
    if (target < tick_) throw std::invalid_argument("cannot rewind time");
    for (auto& component : components_) component->advance_to(target);
    tick_ = target;
}
}
