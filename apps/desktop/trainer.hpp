// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "presentation.hpp"
#include "trainer_config.hpp"
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace rse::desktop {
struct TrainerMetrics {
    std::uint64_t steps{};
    double rate{};
    std::optional<double> reward,loss;
    std::optional<unsigned> action;
};
// The worker owns the child and reads its files. The UI consumes the latest
// bounded snapshot and requests cancellation without waiting on the worker.
class Trainer {
public:
    explicit Trainer(TrainerConfig config);
    ~Trainer();
    void start(const std::filesystem::path& rom,const std::filesystem::path& profile,
               std::string_view observation,const std::filesystem::path& resume={});
    void stop();
    [[nodiscard]] bool poll();
    [[nodiscard]] bool running() const noexcept {return running_;}
    [[nodiscard]] const std::string& status() const noexcept {return status_;}
    [[nodiscard]] const TrainerMetrics& metrics() const noexcept {return metrics_;}
    [[nodiscard]] const Frame& preview() const noexcept {return preview_;}
    [[nodiscard]] const std::filesystem::path& checkpoint() const noexcept {return checkpoint_;}
private:
    struct Run;
    struct Snapshot {
        std::string status;
        TrainerMetrics metrics;
        Frame preview;
        std::filesystem::path checkpoint;
        bool running;
    };
    void publish(Snapshot snapshot);
    TrainerConfig config_;
    std::filesystem::path checkpoint_;
    std::string status_{"Ready"};
    TrainerMetrics metrics_;
    Frame preview_;
    bool running_{};
    std::atomic_bool stop_requested_{};
    std::mutex pending_mutex_;
    std::optional<Snapshot> pending_;
    std::jthread worker_;
};
}
