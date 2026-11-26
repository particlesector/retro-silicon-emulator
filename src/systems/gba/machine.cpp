// SPDX-License-Identifier: Apache-2.0
#include <rse/systems/gba/machine.hpp>

namespace rse::gba {

void Machine::reset() noexcept {
    bus_.reset();
    cpu_.reset();
}

void Machine::step() {
    if (bus_.dma_pending()) {
        bus_.step_dma();
    } else if (bus_.halted()) {
        bus_.idle_to_lcd_event();
    } else {
        const auto before = bus_.ticks();
        cpu_.set_irq(bus_.irq());
        cpu_.step();
        if (bus_.ticks() == before)
            bus_.advance_irq_entry();
    }
}

}
