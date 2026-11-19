// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "session.hpp"
#include <atomic>
#include <memory>

namespace rse::session {
enum class Delivery { lossless, preview };
enum class StopReason { target, budget, paused, step, breakpoint, watchpoint, overflow };
struct Breakpoint {
    Boundary boundary;
    ValueRange value;
    std::uint64_t equals;
};
struct Watchpoint {
    std::string_view event_id;
    ValueRange addresses;
};
struct RunRequest {
    Tick target;
    Tick budget;
    std::optional<Boundary> step;
    std::optional<Breakpoint> breakpoint;
    std::optional<Watchpoint> watchpoint;
    // Zero disables observation output. Stop controls still work.
    std::size_t observation_capacity{};
    Delivery delivery{Delivery::lossless};
};
struct RunResult {
    Tick tick;
    StopReason reason;
    std::vector<std::byte> observations;
};

// Owns its session exclusively. Overlapping and reentrant commands fail
// immediately. Only pause() operates independently of an in-flight command.
class Automation {
public:
    explicit Automation(std::unique_ptr<Session> session);
    [[nodiscard]] const SystemInfo& info() const;
    [[nodiscard]] Status status() const;
    [[nodiscard]] InspectionInfo schema() const;
    [[nodiscard]] std::optional<std::uint64_t> peek(std::string_view id, std::uint32_t address=0) const;
    void read(std::string_view id,std::uint32_t first,std::span<std::optional<std::uint64_t>> output) const;
    void reset();
    [[nodiscard]] std::vector<std::uint8_t> snapshot() const;
    void restore(std::span<const std::uint8_t>);
    void schedule_input(Tick tick, std::string_view port, std::span<const std::string_view> pressed);
    void pause() noexcept;
    void resume();
    [[nodiscard]] RunResult run(const RunRequest& request);
    [[nodiscard]] std::vector<std::int16_t> take_audio();
private:
    class Command;
    Inspection& inspection() const;
    std::unique_ptr<Session> session_;
    mutable std::atomic_flag busy_{};
    std::atomic_bool paused_{};
};
}
