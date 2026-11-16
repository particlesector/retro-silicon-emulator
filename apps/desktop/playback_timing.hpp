// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <rse/core/component.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>

// Existing NTSC frontend policy, named without changing its values. M10/M11
// will move system clock conversion behind the machine-session boundary.
namespace rse::desktop::timing {
inline constexpr int sample_rate=48'000;
inline constexpr std::size_t sample_bytes=sizeof(std::int16_t);
inline constexpr unsigned source_margin_samples=96; // 2 ms at 48 kHz
inline constexpr Tick producer_chunk_ticks=21'480; // about 1 ms NTSC master time
inline constexpr unsigned producer_batch_limit=32;
inline constexpr auto producer_poll_interval=std::chrono::milliseconds(1);
inline constexpr unsigned ui_idle_ms=1;
inline constexpr std::uint64_t master_ticks_per_11_ms=236'250;
inline constexpr std::uint64_t simulated_ms(Tick ticks) noexcept {
    return ticks*11/master_ticks_per_11_ms;
}
}
