// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <rse/systems/nes/machine.hpp>
#include <SDL3/SDL.h>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace rse::desktop {
struct PlaybackStats {
    std::uint64_t ticks{},samples{},frames{},max_chunk_us{};
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
    void replace(std::unique_ptr<nes::Machine> machine);
    void reset();
    void clear();
    void active(bool value);
    // Commands and snapshots are serialized with one bounded producer batch.
    // ready() becomes true after priming; only then resume the SDL device.
    void buttons(std::uint8_t value);
    [[nodiscard]] bool loaded() const;
    [[nodiscard]] bool ready() const;
    [[nodiscard]] Tick tick() const;
    [[nodiscard]] nes::MachineState state() const;
    [[nodiscard]] std::optional<std::uint8_t> memory(std::uint16_t address) const;
    bool update_frame(video::PpuFrame& frame) const;
    [[nodiscard]] PlaybackStats stats() const;
    [[nodiscard]] std::string error() const;
private:
    void produce();
    unsigned queued() const;
    void reset_timeline();
    SDL_AudioStream* stream_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::unique_ptr<nes::Machine> machine_;
    video::PpuFrame latest_;
    PlaybackStats stats_;
    std::uint8_t buttons_{};
    std::optional<std::uint8_t> scheduled_buttons_;
    bool active_{},ready_{},quit_{};
    std::string error_;
    std::thread worker_;
};
}
