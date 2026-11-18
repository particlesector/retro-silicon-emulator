// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "session_output.hpp"
#include <functional>
#include <SDL3/SDL.h>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace rse::desktop {
struct PlaybackStats {
    std::uint64_t ticks{},samples{},frames{},max_chunk_us{},simulated_ms{};
    unsigned peak_audio{},peak_video{},target_bytes{};
    bool high_priority{};
};
// Frontend-owned producer. The machine is accessed only under mutex_, never
// concurrently; SDL events and all GPU work remain on the application's thread.
class Playback {
public:
    explicit Playback(SDL_AudioStream* stream);
    ~Playback();
    // Before replacing/resetting: active(false), pause the SDL device, then
    // clear its stream. The application owns that stream and its callbacks.
    void replace(std::unique_ptr<session::Session> machine);
    void reset();
    void clear();
    void active(bool value);
    // Commands and snapshots are serialized with one bounded producer batch.
    // ready() becomes true after priming; only then resume the SDL device.
    // Latest complete input snapshot; omitted ports are released. Requests are
    // validated atomically against metadata and replace one bounded snapshot.
    void input(std::vector<PortInput> value);
    [[nodiscard]] bool loaded() const;
    [[nodiscard]] bool ready() const;
    [[nodiscard]] Tick tick() const;
    [[nodiscard]] session::Status state() const;
    [[nodiscard]] bool has_audio() const;
    // Read-only diagnostic callback under the producer lock. Do not reenter
    // Playback or retain borrowed session views after the callback returns.
    [[nodiscard]] bool inspect(const std::function<bool(const session::Session&)>& query) const;
    bool update_frame(Frame& frame) const;
    [[nodiscard]] PlaybackStats stats() const;
    [[nodiscard]] std::string error() const;
private:
    void produce();
    unsigned queued() const;
    void reset_timeline();
    SDL_AudioStream* stream_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::unique_ptr<session::Session> machine_;
    Frame latest_;
    PlaybackStats stats_;
    std::vector<PortInput> inputs_;
    std::optional<std::vector<PortInput>> scheduled_inputs_;
    Tick chunk_ticks_{};
    unsigned device_rate_{},device_frames_{};
    std::uint64_t next_silent_ns_{},silent_period_ns_{};
    long double simulated_ms_{};
    bool active_{},ready_{},quit_{};
    std::string error_;
    std::thread worker_;
};
}
