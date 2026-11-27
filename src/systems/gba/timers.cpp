// SPDX-License-Identifier: Apache-2.0
#include <rse/systems/gba/timers.hpp>
#include <algorithm>

namespace rse::gba {

unsigned Timers::Counter::period() const noexcept {
    constexpr unsigned periods[] = {1, 64, 256, 1024};
    return periods[control & 3];
}

std::uint16_t Timers::Counter::read(std::uint64_t tick) const noexcept {
    return running() && !cascades() ? std::uint16_t(value + (tick - origin) / period()) : value;
}

void Timers::Counter::reschedule(std::uint64_t tick) noexcept {
    const auto divider = period();
    origin = tick & ~std::uint64_t(divider - 1);
    const auto until_wrap = (0x10000u - value) * divider;
    next = running() && !cascades() && until_wrap < never - origin ? origin + until_wrap : never;
}

void Timers::select_next() noexcept {
    next_ = never;
    for (const auto& counter : counters_) next_ = std::min(next_, counter.next);
}

std::uint16_t Timers::read(unsigned offset, std::uint64_t tick) const noexcept {
    const auto& counter = counters_[offset / 4];
    return (offset & 2) ? counter.control : counter.read(tick);
}

void Timers::write(unsigned offset, std::uint16_t value, std::uint16_t mask, std::uint64_t tick) noexcept {
    auto& counter = counters_[offset / 4];
    const auto merge = [=](std::uint16_t old) { return (old & ~mask) | (value & mask); };
    if (!(offset & 2)) {
        counter.reload = merge(counter.reload);
        return;
    }
    const bool running = counter.running();
    const auto control = merge(counter.control) & (offset < 4 ? 0xc3 : 0xc7);
    if (control == counter.control) return;
    counter.value = counter.read(tick);
    counter.control = control;
    if (!running && counter.running()) counter.value = counter.reload;
    counter.reschedule(tick);
    select_next();
}

unsigned Timers::overflow(std::uint64_t tick) noexcept {
    unsigned wrapped = 0;
    bool carry = false;
    for (unsigned index = 0; index < counters_.size(); ++index) {
        auto& counter = counters_[index];
        const bool cascaded = carry && counter.running() && counter.cascades();
        if (cascaded) ++counter.value;
        carry = (cascaded && counter.value == 0) || counter.next == tick;
        if (carry) {
            wrapped |= 1u << index;
            counter.value = counter.reload;
            counter.reschedule(tick);
        }
    }
    select_next();
    return wrapped;
}

}
