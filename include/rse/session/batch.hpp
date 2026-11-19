// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "session.hpp"
#include <exception>
#include <functional>
#include <memory>
#include <string>

namespace rse::session {
struct BatchRange {
    std::string id;
    std::uint32_t first{},count{};
};
struct BatchOptions {
    std::size_t environments{},workers{};
    std::uint64_t seed{};
    std::vector<BatchRange> reads;
    bool video{};
};
struct BatchAction {
    std::string_view port;
    std::span<const std::string_view> pressed;
};
struct BatchFrame {
    std::uint64_t number;
    std::size_t pitch_bytes;
    std::vector<std::byte> pixels;
};
struct BatchResult {
    Status status;
    std::vector<std::optional<std::uint64_t>> values;
    std::optional<BatchFrame> frame;
    std::exception_ptr error;
};

// A caller-driven, fixed-size pool of exclusively owned, headless sessions.
// Calls may not overlap. See docs/PARALLEL_HEADLESS.md for reset and stop rules.
class Batch {
public:
    using Factory=std::function<std::unique_ptr<Session>(std::size_t index,std::uint64_t seed)>;
    explicit Batch(BatchOptions options,Factory factory);
    ~Batch();
    Batch(const Batch&)=delete;
    Batch& operator=(const Batch&)=delete;
    // Reset by recreating a slot with its original seed or an explicit replacement.
    void reset(std::size_t index,std::optional<std::uint64_t> seed={});
    [[nodiscard]] std::uint64_t seed(std::size_t index) const;
    // Empty actions means no change for any slot. A present action replaces
    // that port's pressed set at the slot's first tick in this step.
    [[nodiscard]] std::vector<BatchResult> step(
        Tick ticks,std::span<const std::optional<BatchAction>> actions={});
    [[nodiscard]] std::vector<std::uint8_t> snapshot(std::size_t index) const;
    [[nodiscard]] static std::uint64_t derive_seed(std::uint64_t seed,std::size_t index) noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
