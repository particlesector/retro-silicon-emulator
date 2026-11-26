// SPDX-License-Identifier: Apache-2.0
#include <rse/systems/gba/session.hpp>
#include <algorithm>

namespace rse::gba {

const session::SystemInfo& Session::info() const noexcept {
    static constexpr session::SystemInfo info{"gba", "Game Boy Advance",
        {clock_hz, 1}, std::nullopt, std::nullopt, {}, false};
    return info;
}

void Session::reset() {
    machine_.reset();
    status_ = {};
}

void Session::advance_to(Tick target) {
    if (target < status_.tick) throw std::invalid_argument("Cannot rewind a GBA session");
    if (target == status_.tick) return;
    if (status_.faulted) throw std::logic_error("Reset the faulted GBA session before advancing");
    try {
        while (machine_.bus().ticks() < target) machine_.step();
        status_.tick = target;
    } catch (...) {
        status_.tick = std::min(machine_.bus().ticks(), target);
        status_.faulted = true;
        throw;
    }
}

std::optional<EntryFetch> Session::entry_fetch() const noexcept {
    const auto entry = machine_.bus().entry_fetch();
    if (entry && entry->tick < status_.tick) return entry;
    return std::nullopt;
}

}
