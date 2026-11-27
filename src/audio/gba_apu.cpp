// SPDX-License-Identifier: Apache-2.0
#include <rse/audio/gba_apu.hpp>
#include <algorithm>

namespace rse::audio {
namespace {
constexpr unsigned length_reg[] = {0x02, 0x08, 0x12, 0x18};
constexpr unsigned frequency_reg[] = {0x04, 0x0c, 0x14, 0x1c};
constexpr unsigned lengths[] = {64, 64, 256, 64};
constexpr unsigned sequencer_period = 32768;
}

void GbaApu::Fifo::push(std::uint8_t value) noexcept {
    if (size == bytes.size()) return;
    bytes[(head + size) % bytes.size()] = static_cast<std::int8_t>(value);
    ++size;
}

void GbaApu::Fifo::pop() noexcept {
    if (!size) return;
    output = bytes[head];
    head = (head + 1) % bytes.size();
    --size;
}

unsigned GbaApu::wave_bank() const noexcept {
    const auto selected = (reg(wave_control) >> 6) & 1;
    const auto playing = (reg(wave_control) & 0x20) ? (voices_[2].phase / 32) ^ selected : selected;
    return playing ^ 1;
}

std::uint16_t GbaApu::read(unsigned offset) const noexcept {
    if (offset >= fifo_a) return 0;
    if (offset >= wave_ram) {
        const auto index = 16 * wave_bank() + offset - wave_ram;
        return wave_[index] | (std::uint16_t(wave_[index + 1]) << 8);
    }
    if (offset == bias) return bias_;
    if (offset == enable) {
        unsigned status = reg(enable);
        for (unsigned i = 0; i < voices_.size(); ++i) if (voices_[i].active) status |= 1u << i;
        return std::uint16_t(status);
    }
    constexpr std::uint16_t masks[] = {
        0x007f,0xffc0,0x4000,0, 0xffc0,0,0x4000,0, 0x00e0,0xe000,0x4000,0,
        0xff00,0,0x40ff,0, 0xff77,0x770f,0,0, 0,0,0,0,
    };
    return reg(offset) & masks[offset / 2];
}

void GbaApu::write(unsigned offset, std::uint16_t value, std::uint16_t mask, std::uint64_t tick) noexcept {
    advance(tick);
    const auto merged = [=](std::uint16_t prior) { return (prior & ~mask) | (value & mask); };
    if (offset >= fifo_a) {
        auto& fifo = fifo_[(offset - fifo_a) / 4];
        if (mask & 0x00ff) fifo.push(std::uint8_t(value));
        if (mask & 0xff00) fifo.push(std::uint8_t(value >> 8));
        return;
    }
    if (offset >= wave_ram) {
        const auto index = 16 * wave_bank() + offset - wave_ram;
        if (mask & 0x00ff) wave_[index] = std::uint8_t(value);
        if (mask & 0xff00) wave_[index + 1] = std::uint8_t(value >> 8);
        return;
    }
    if (offset == bias) {
        bias_ = merged(bias_) & 0xc3fe;
        return;
    }
    if (offset == enable) {
        const auto next = merged(reg(enable)) & 0x80;
        if (!next && enabled()) {
            std::fill_n(registers_.begin(), mix_psg / 2 + 1, 0);
            voices_ = {};
            sweep_negated_ = false;
        }
        if (next && !enabled()) sequencer_phase_ = sequencer_ticks_ = 0;
        reg(enable) = std::uint16_t(next);
        return;
    }
    if (offset == mix_direct) {
        const auto bits = merged(reg(offset));
        if (bits & 0x0800) fifo_[0] = {};
        if (bits & 0x8000) fifo_[1] = {};
        reg(offset) = bits & 0x770f;
        return;
    }
    if (!enabled()) return;
    const auto prior = reg(offset);
    reg(offset) = std::uint16_t(merged(prior));
    if (offset == sweep && sweep_negated_ && !(reg(sweep) & 8)) voices_[0].active = false;
    if (offset == wave_control && !(reg(offset) & 0x80)) voices_[2].active = false;
    for (unsigned channel = 0; channel < voices_.size(); ++channel) {
        if (offset == length_reg[channel]) {
            if (mask & 0x00ff) voices_[channel].length = lengths[channel] - (value & (lengths[channel] - 1));
            if (channel != 2 && !(reg(offset) & 0xf800)) voices_[channel].active = false;
        }
        if (offset == frequency_reg[channel] && (value & mask & 0x8000)) trigger(channel);
    }
    reg(offset) &= (offset == 0x04 || offset == 0x0c || offset == 0x14 || offset == 0x1c) ? 0x7fff : 0xffff;
}

unsigned GbaApu::period(unsigned channel) const noexcept {
    const auto frequency = reg(frequency_reg[channel]);
    if (channel != 3) return (2048 - (frequency & 2047)) * (channel == 2 ? 8 : 16);
    const unsigned divisor = frequency & 7;
    return (divisor ? divisor * 64 : 32) << ((frequency >> 4) & 15);
}

void GbaApu::clock_voices(unsigned ticks) noexcept {
    for (unsigned channel = 0; channel < voices_.size(); ++channel) {
        auto& voice = voices_[channel];
        if (!voice.active) continue;
        const auto interval = period(channel);
        const auto clocks = voice.remainder + ticks;
        const auto steps = clocks / interval;
        voice.remainder = clocks % interval;
        if (channel != 3) {
            voice.phase = (voice.phase + steps) % (channel == 2 ? ((reg(wave_control) & 0x20) ? 64 : 32) : 8);
        } else {
            for (unsigned i = 0; i < steps; ++i) {
                const auto feedback = (noise_shift_ ^ (noise_shift_ >> 1)) & 1;
                noise_shift_ = (noise_shift_ >> 1) | (feedback << 14);
                if (reg(frequency_reg[3]) & 8) noise_shift_ = (noise_shift_ & ~0x40) | (feedback << 6);
            }
        }
    }
}

void GbaApu::clock_sweep() noexcept {
    const auto config = reg(sweep);
    const auto interval = (config >> 4) & 7, shift = config & 7;
    if (!voices_[0].active || !interval || !shift || --sweep_clock_) return;
    sweep_clock_ = interval;
    const bool subtract = config & 8;
    const auto delta = sweep_frequency_ >> shift;
    const auto next = subtract ? sweep_frequency_ - delta : sweep_frequency_ + delta;
    sweep_negated_ |= subtract;
    if (next <= 2047) {
        sweep_frequency_ = next;
        reg(frequency_reg[0]) = (reg(frequency_reg[0]) & ~0x07ff) | next;
    }
    if (next > 2047 || (!subtract && next + (next >> shift) > 2047)) voices_[0].active = false;
}

void GbaApu::clock_frame_sequencer() noexcept {
    if (sequencer_phase_ == 2 || sequencer_phase_ == 6) clock_sweep();
    for (unsigned channel = 0; channel < voices_.size(); ++channel) {
        auto& voice = voices_[channel];
        if (!voice.active) continue;
        if (!(sequencer_phase_ & 1) && (reg(frequency_reg[channel]) & 0x4000) &&
            voice.length && !--voice.length) voice.active = false;
        if (channel == 2 || sequencer_phase_ != 7) continue;
        const auto envelope = reg(length_reg[channel]) >> 8;
        const unsigned interval = envelope & 7;
        if (interval && ++voice.envelope_clock >= interval) {
            voice.envelope_clock = 0;
            if ((envelope & 8) && voice.volume < 15) ++voice.volume;
            else if (!(envelope & 8) && voice.volume > 0) --voice.volume;
        }
    }
    sequencer_phase_ = (sequencer_phase_ + 1) % 8;
}

void GbaApu::advance(std::uint64_t tick) noexcept {
    auto remaining = tick - tick_;
    tick_ = tick;
    if (!enabled()) return;
    while (remaining) {
        const auto span = unsigned(std::min<std::uint64_t>(remaining, sequencer_period - sequencer_ticks_));
        clock_voices(span);
        remaining -= span;
        sequencer_ticks_ += span;
        if (sequencer_ticks_ == sequencer_period) {
            sequencer_ticks_ = 0;
            clock_frame_sequencer();
        }
    }
}

void GbaApu::trigger(unsigned channel) noexcept {
    auto& voice = voices_[channel];
    const auto envelope = reg(length_reg[channel]) >> 8;
    voice.active = channel == 2 ? (reg(wave_control) & 0x80) != 0 : (envelope & 0xf8) != 0;
    if (!voice.length) voice.length = lengths[channel];
    voice.phase = voice.remainder = voice.envelope_clock = 0;
    voice.volume = envelope >> 4;
    if (channel == 0) {
        sweep_frequency_ = reg(frequency_reg[0]) & 2047;
        const auto sweep_config = reg(sweep);
        sweep_clock_ = ((sweep_config >> 4) & 7) ? ((sweep_config >> 4) & 7) : 8;
        sweep_negated_ = false;
        const auto shift = sweep_config & 7;
        if (shift && !(sweep_config & 8) && sweep_frequency_ + (sweep_frequency_ >> shift) > 2047)
            voice.active = false;
    }
    if (channel == 3) noise_shift_ = 0x7fff;
}

int GbaApu::level(unsigned channel) const noexcept {
    const auto& voice = voices_[channel];
    if (!voice.active) return 0;
    if (channel == 2) {
        const auto volume = reg(length_reg[2]) >> 13;
        const unsigned offset = 16 * (wave_bank() ^ 1) + (voice.phase & 31) / 2;
        const int sample = int((wave_[offset] >> (voice.phase & 1 ? 0 : 4)) & 15) - 8;
        constexpr int factors[] = {0, 4, 2, 1, 3, 3, 3, 3};
        return sample * factors[volume] / 4;
    }
    constexpr unsigned duty[] = {0x80, 0x81, 0xe1, 0x7e};
    const bool high = channel == 3 ? !(noise_shift_ & 1) :
        (duty[(reg(length_reg[channel]) >> 6) & 3] & (1u << voice.phase)) != 0;
    return high ? int(voice.volume) : -int(voice.volume);
}

unsigned GbaApu::timer_overflow(unsigned timer) noexcept {
    if (!enabled()) return 0;
    unsigned requests = 0;
    for (unsigned channel = 0; channel < fifo_.size(); ++channel) {
        const auto control = reg(mix_direct) >> (8 + 4 * channel);
        if (!(control & 3) || ((control >> 2) & 1) != timer) continue;
        fifo_[channel].pop();
        if (fifo_[channel].size <= 16) requests |= 1u << channel;
    }
    return requests;
}

std::array<std::int16_t, 2> GbaApu::sample(std::uint64_t tick) noexcept {
    advance(tick);
    if (!enabled()) return {};
    const auto psg = reg(mix_psg), direct = reg(mix_direct);
    const auto center = int(bias_ & 0x03ff);
    std::array<int, 4> levels{};
    for (unsigned i = 0; i < levels.size(); ++i) levels[i] = level(i);
    std::array<std::int16_t, 2> pcm{};
    for (unsigned side = 0; side < 2; ++side) {
        const unsigned shift = 4 * (1 - side); // SOUNDCNT_L puts right before left.
        int value = 0;
        for (unsigned channel = 0; channel < levels.size(); ++channel)
            if (psg & (1u << (8 + shift + channel))) value += levels[channel];
        constexpr int ratios[] = {1, 2, 4, 0};
        value = value * (int((psg >> shift) & 7) + 1) * ratios[direct & 3] / 4;
        for (unsigned channel = 0; channel < fifo_.size(); ++channel)
            if (direct & (1u << (9 - side + 4 * channel)))
                value += fifo_[channel].output * ((direct & (4u << channel)) ? 2 : 1);
        const auto dac = std::clamp(center + value, 0, 1023);
        pcm[side] = std::int16_t(std::clamp((dac - center) * 64, -32768, 32767));
    }
    return pcm;
}

}
