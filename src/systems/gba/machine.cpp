// SPDX-License-Identifier: Apache-2.0
#include <rse/systems/gba/machine.hpp>

namespace rse::gba {

void Machine::reset() noexcept {
    bus_.reset();
    cpu_.reset();
    cpu_in_progress_ = false;
}

void Machine::step() {
    step_until(Timers::never);
}

void Machine::advance_to(std::uint64_t tick) {
    while (bus_.ticks() < tick) step_until(tick);
}

void Machine::step_until(std::uint64_t limit) {
    if (!cpu_in_progress_ && bus_.dma_pending()) {
        bus_.step_dma(limit);
        return;
    }
    if (!cpu_in_progress_ && bus_.halted()) {
        bus_.idle(limit);
        return;
    }
    const auto before = bus_.ticks();
    if (!cpu_in_progress_) cpu_.set_irq(bus_.irq());
    bus_.begin_cpu_attempt(limit);
    cpu_in_progress_ = !cpu_.step_with_stalls();
    if (!cpu_in_progress_) {
        bus_.complete_cpu_attempt();
        if (bus_.ticks() == before) bus_.advance_irq_entry();
    }
}

}
