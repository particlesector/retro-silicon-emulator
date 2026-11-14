// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace rse::audio {
struct Envelope {
    std::uint8_t control{}, divider{}, decay{};
    bool start{};
    void clock() noexcept;
    [[nodiscard]] std::uint8_t output() const noexcept;
};
struct PulseState {
    Envelope envelope;
    std::uint16_t period{}, timer{};
    std::uint8_t length{}, phase{}, sweep{}, sweep_divider{};
    bool enabled{}, sweep_reload{};
};
struct TriangleState {
    std::uint16_t period{}, timer{};
    std::uint8_t control{}, length{}, linear{}, phase{15};
    bool enabled{}, reload{};
};
struct NoiseState {
    Envelope envelope;
    std::uint16_t timer{}, shift{1};
    std::uint8_t period{}, length{};
    bool enabled{}, mode{};
};
struct DmcState {
    std::uint16_t timer{213}, address{0xc000}, remaining{};
    std::uint8_t control{}, address_load{}, length_load{}, output{}, buffer{}, shift{}, bits{8};
    bool buffer_full{}, silence{true}, irq{};
};
struct ApuState {
    std::array<PulseState,2> pulse;
    TriangleState triangle;
    NoiseState noise;
    DmcState dmc;
    std::uint64_t cycles{}, quarters{}, halves{};
    std::uint32_t frame_cycle{};
    bool five_step{}, inhibit{}, frame_irq{};
};
struct ChannelOutput { std::uint8_t pulse1{}, pulse2{}, triangle{}, noise{}, dmc{}; };

// Sound circuitry only. DMA accesses, IRQ wiring, console decode, and PCM
// conversion are external. See docs/APU_2A03.md for ordering and limits.
class Rp2a03Apu {
public:
    void clock(bool get_phase);
    void write(std::uint8_t reg, std::uint8_t value);
    // Only $15 is readable. Bit 5 is supplied by the console's open bus.
    [[nodiscard]] std::uint8_t read_status() noexcept;
    [[nodiscard]] bool irq_asserted() const noexcept;
    [[nodiscard]] ChannelOutput output() const noexcept;
    [[nodiscard]] const ApuState& state() const noexcept { return state_; }
    // Address becomes available on the load/reload halt phase. It remains
    // pending until accept_dma(); cancel before halting when it disappears.
    [[nodiscard]] std::optional<std::uint16_t> dma_request() const noexcept;
    void accept_dma(std::uint8_t data);
private:
    ApuState state_;
    bool get_{}, pending_five_{};
    std::uint8_t reset_delay_{};
    std::optional<std::uint64_t> request_at_;
    bool request_reload_{};
    void quarter() noexcept;
    void half() noexcept;
    void restart_sample() noexcept;
    void request_sample(bool reload) noexcept;
};
} // namespace rse::audio
