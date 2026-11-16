// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <rse/core/component.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>

// Host policy. Source format and clock conversion come from session metadata.
namespace rse::desktop::timing {
inline constexpr std::size_t sample_bytes=sizeof(std::int16_t);
inline constexpr unsigned source_margin_ms=2;
inline constexpr unsigned producer_chunk_us=1000;
inline constexpr unsigned producer_batch_limit=32;
inline constexpr auto producer_poll_interval=std::chrono::milliseconds(1);
inline constexpr unsigned ui_idle_ms=1;
}
