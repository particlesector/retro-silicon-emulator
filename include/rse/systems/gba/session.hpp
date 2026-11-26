// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "machine.hpp"
#include <rse/session/session.hpp>

namespace rse::gba {

// Headless GBA adapter with exact board-clock advance and VBlank publications.
class Session final : public session::Session {
public:
    Session(std::span<const std::uint8_t> bios, std::span<const std::uint8_t> cartridge)
        : machine_(bios, cartridge) {}
    const session::SystemInfo& info() const noexcept override;
    session::Status status() const noexcept override;
    void reset() override;
    void advance_to(Tick target) override;
    void run_frame() override;
    void schedule_input(Tick tick, std::string_view port, std::span<const std::string_view> pressed) override;
    std::optional<session::VideoFrame> video() const noexcept override;
    void convert_video(std::span<session::Rgba> output) const override;
    [[nodiscard]] std::optional<EntryFetch> entry_fetch() const noexcept;

private:
    Machine machine_;
    bool faulted_{};
};

}
