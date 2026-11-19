// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <rse/audio/rp2a03_apu.hpp>
#include <algorithm>
#include <cstdint>
#include <optional>

namespace rse::audio {
// Deterministic 48 kHz mono PCM for an NTSC CPU clock of 19,687,500/11 Hz.
// Integer nonlinear LUT approximation, fractional box integration, DC blocker.
// No host-clock dependencies or analog filter equivalence claims.
class NtscPcm {
public:
    static constexpr unsigned sample_rate=48000;
    [[nodiscard]] static std::int32_t mix(ChannelOutput c) noexcept {
        const std::uint64_t p=c.pulse1+c.pulse2, t=3*c.triangle+2*c.noise+c.dmc;
        return static_cast<std::int32_t>(9552*p*32767/(812800+10000*p)+
            16367*t*32767/(2432900+10000*t));
    }
    [[nodiscard]] std::optional<std::int16_t> clock(ChannelOutput channels) noexcept {
        constexpr std::uint32_t width=19'687'500, step=sample_rate*11;
        const auto value=mix(channels);
        const auto first=std::min(step,width-phase_);
        sum_+=std::int64_t(value)*first; phase_+=first;
        if (phase_<width) return std::nullopt;
        const auto average=static_cast<std::int32_t>(sum_/width);
        // Q15 first-order DC blocker; truncation is explicitly toward zero.
        high_=average-previous_+static_cast<std::int32_t>(std::int64_t(high_)*32384/32768);
        previous_=average;
        phase_=step-first; sum_=std::int64_t(value)*phase_;
        return static_cast<std::int16_t>(std::clamp(high_,-32768,32767));
    }
    void save_state(rse::state::Writer& out) const {out(phase_,sum_,previous_,high_);}
    void load_state(rse::state::Reader& in) {
        NtscPcm next;
        in(next.phase_,next.sum_,next.previous_,next.high_);
        rse::state::require(next.phase_<19'687'500 && next.sum_>=0 &&
            next.sum_<=std::int64_t(next.phase_)*32767 && next.previous_>=0 &&
            next.previous_<=32767 && next.high_>=-65536 && next.high_<=65536);
        *this=next;
    }
private:
    std::uint32_t phase_{};
    std::int64_t sum_{};
    std::int32_t previous_{}, high_{};
};
} // namespace rse::audio
