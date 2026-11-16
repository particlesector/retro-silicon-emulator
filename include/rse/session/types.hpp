// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <rse/core/component.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace rse::session {
struct Rational {
    std::uint64_t numerator{},denominator{1};
    bool operator==(const Rational&) const=default;
};
struct Rgba {
    std::uint8_t r{},g{},b{},a{255};
    bool operator==(const Rgba&) const=default;
};
// Borrowed byte views are in-process data, not a serialized image format.
enum class PixelFormat { indexed_u16_native,rgba8 };
struct VideoFormat {
    unsigned width{},height{};
    Rational display_aspect;
    PixelFormat pixels{};
};
enum class SampleFormat { signed16_native };
struct AudioFormat {
    unsigned sample_rate{},channels{};
    SampleFormat samples{SampleFormat::signed16_native};
    std::size_t max_buffered_samples{}; // Individual interleaved samples.
};
struct InputPort {
    std::string_view id;
    std::span<const std::string_view> actions;
};
struct SystemInfo {
    std::string_view id,name;
    Rational clock_hz;
    std::optional<VideoFormat> video;
    std::optional<AudioFormat> audio;
    std::span<const InputPort> input_ports;
    bool frame_step{};
};
struct Status {
    Tick tick{};
    std::uint64_t video_frames{}; // Zero for a system without video.
    bool faulted{};
};
struct VideoFrame {
    std::uint64_t number{};
    std::size_t pitch_bytes{};
    std::span<const std::byte> pixels;
};
}
