// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <rse/session/session.hpp>
#include <rse/systems/nes/machine.hpp>

namespace rse::nes {
class Session final : public session::Session {
public:
    // The adapter always captures PCM; the profile controls its queue bound and
    // optional NES trace diagnostics. The existing machine validates the profile.
    explicit Session(Cartridge cartridge,Profile profile={});
    [[nodiscard]] const session::SystemInfo& info() const noexcept override {return info_;}
    [[nodiscard]] session::Status status() const noexcept override;
    [[nodiscard]] session::Inspection* inspection() noexcept override {return &machine_;}
    void reset() override;
    [[nodiscard]] std::vector<std::uint8_t> snapshot() const override {return machine_.snapshot();}
    void restore(std::span<const std::uint8_t> bytes) override {machine_.restore(bytes);}
    void advance_to(Tick target) override;
    void run_frame() override;
    void schedule_input(Tick tick,std::string_view port,std::span<const std::string_view> pressed) override;
    [[nodiscard]] std::optional<session::VideoFrame> video() const noexcept override;
    void convert_video(std::span<session::Rgba> output) const override;
    [[nodiscard]] std::vector<std::int16_t> take_audio() override;
    // Read-only chip-specific inspection, deliberately outside the common API.
    // Borrowed trace/frame data is invalidated by subsequent session mutations.
    [[nodiscard]] const Machine& diagnostics() const noexcept {return machine_;}
private:
    Machine machine_;
    session::SystemInfo info_;
};
}
