// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "machine.hpp"
#include <rse/session/session.hpp>

namespace rse::gba {

// Headless GBA boot adapter. The machine can execute ahead of the host tick
// within one instruction or transfer. Status reports the requested host tick.
class Session final : public session::Session {
public:
    Session(std::span<const std::uint8_t> bios, std::span<const std::uint8_t> cartridge)
        : machine_(bios, cartridge) {}
    const session::SystemInfo& info() const noexcept override;
    session::Status status() const noexcept override { return status_; }
    void reset() override;
    void advance_to(Tick target) override;
    [[nodiscard]] std::optional<EntryFetch> entry_fetch() const noexcept;

private:
    Machine machine_;
    session::Status status_{};
};

}
