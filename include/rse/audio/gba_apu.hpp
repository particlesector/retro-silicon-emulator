// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <array>
#include <cstdint>

namespace rse::audio {

// GBA sound generators and stereo mixer. The board owns FIFO DMA requests,
// timer routing and the fixed host PCM sampling schedule.
class GbaApu {
public:
    static constexpr unsigned sample_period = 512, sample_rate = 32768;
    std::uint16_t read(unsigned offset) const noexcept;
    void write(unsigned offset, std::uint16_t value, std::uint16_t mask, std::uint64_t tick) noexcept;
    // Advance the selected Direct Sound engines. Bit 0/1 requests FIFO A/B.
    unsigned timer_overflow(unsigned timer) noexcept;
    std::array<std::int16_t, 2> sample(std::uint64_t tick) noexcept;

private:
    struct Voice {
        bool active{};
        unsigned length{}, phase{}, remainder{}, volume{}, envelope_clock{};
    };
    struct Fifo {
        std::array<std::int8_t,32> bytes{};
        unsigned head{}, size{};
        int output{};
        void push(std::uint8_t sample) noexcept;
        void pop() noexcept;
    };

    static constexpr unsigned sweep = 0x00, wave_control = 0x10, mix_psg = 0x20,
                              mix_direct = 0x22, enable = 0x24, bias = 0x28, wave_ram = 0x30, fifo_a = 0x40;
    bool enabled() const noexcept { return reg(enable) & 0x80; }
    std::uint16_t reg(unsigned offset) const noexcept { return registers_[offset / 2]; }
    std::uint16_t& reg(unsigned offset) noexcept { return registers_[offset / 2]; }
    unsigned wave_bank() const noexcept;
    unsigned period(unsigned channel) const noexcept;
    void clock_voices(unsigned ticks) noexcept;
    void clock_frame_sequencer() noexcept;
    void advance(std::uint64_t tick) noexcept;
    void trigger(unsigned channel) noexcept;
    void clock_sweep() noexcept;
    int level(unsigned channel) const noexcept;

    std::array<std::uint16_t,0x18> registers_{};
    std::array<std::uint8_t,32> wave_{};
    std::array<Voice,4> voices_{};
    std::array<Fifo,2> fifo_{};
    std::uint64_t tick_{};
    unsigned sequencer_phase_{}, sequencer_ticks_{}, sweep_clock_{}, sweep_frequency_{};
    bool sweep_negated_{};
    std::uint16_t noise_shift_{0x7fff};
    std::uint16_t bias_{0x0200};
};

}
