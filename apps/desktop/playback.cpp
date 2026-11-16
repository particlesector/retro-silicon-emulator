// SPDX-License-Identifier: Apache-2.0
#include "playback.hpp"
#include "playback_timing.hpp"
#include <algorithm>
#include <stdexcept>

namespace rse::desktop {
Playback::Playback(SDL_AudioStream* stream):stream_(stream) {
    SDL_AudioSpec device{};
    int frames{};
    if(!SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream),&device,&frames))
        throw std::runtime_error(SDL_GetError());
    if(device.freq<=0 || frames<=0)
        throw std::runtime_error("Audio device reported an invalid period");

    // One negotiated device request plus the existing 2 ms source margin.
    const auto period_samples=(std::uint64_t(frames)*timing::sample_rate+device.freq-1)/device.freq;
    stats_.target_bytes=unsigned(period_samples+timing::source_margin_samples)*timing::sample_bytes;
    worker_=std::thread([this]{produce();});
}
Playback::~Playback() {
    {
        std::lock_guard lock(mutex_);
        quit_=true;
    }
    // Never join while holding the mutex needed by the producer to exit.
    wake_.notify_one();
    worker_.join();
}
void Playback::reset_timeline() {
    latest_={};
    ready_=false;
    buttons_=0;
    scheduled_buttons_.reset();
    error_.clear();
}
void Playback::replace(std::unique_ptr<nes::Machine> machine) {
    std::lock_guard lock(mutex_);
    machine_=std::move(machine);
    reset_timeline();
}
void Playback::reset() {
    std::lock_guard lock(mutex_);
    if(machine_)machine_->reset();
    reset_timeline();
}
void Playback::clear() {
    std::lock_guard lock(mutex_);
    machine_.reset();
    reset_timeline();
}
void Playback::active(bool value) {
    {
        std::lock_guard lock(mutex_);
        active_=value;
        if(!value)ready_=false; // Every resume must prime before unpausing SDL.
    }
    wake_.notify_one();
}
void Playback::buttons(std::uint8_t value) {
    std::lock_guard lock(mutex_);
    buttons_=value;
}
bool Playback::loaded() const {
    std::lock_guard lock(mutex_);
    return bool(machine_);
}
bool Playback::ready() const {
    std::lock_guard lock(mutex_);
    return ready_;
}
Tick Playback::tick() const {
    std::lock_guard lock(mutex_);
    return machine_?machine_->state().tick:0;
}
nes::MachineState Playback::state() const {
    std::lock_guard lock(mutex_);
    return machine_?machine_->state():nes::MachineState{};
}
std::optional<std::uint8_t> Playback::memory(std::uint16_t address) const {
    std::lock_guard lock(mutex_);
    return machine_?machine_->inspect_memory(address):std::nullopt;
}
bool Playback::update_frame(video::PpuFrame& frame) const {
    std::lock_guard lock(mutex_);
    if(frame.number==latest_.number)return false;
    frame=latest_;
    return true;
}
PlaybackStats Playback::stats() const {
    std::lock_guard lock(mutex_);
    return stats_;
}
std::string Playback::error() const {
    std::lock_guard lock(mutex_);
    return error_;
}
unsigned Playback::queued() const {
    const int count=SDL_GetAudioStreamQueued(stream_);
    if(count<0)throw std::runtime_error(SDL_GetError());
    return unsigned(count);
}
void Playback::produce() {
    // The mutex covers machine access and a bounded refill batch. Waiting
    // releases it so commands, snapshots and shutdown can proceed.
    std::unique_lock lock(mutex_);
#ifdef SDL_PLATFORM_WINDOWS
    stats_.high_priority=SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH);
#endif
    const auto can_produce=[this]{return active_ && machine_ && error_.empty();};
    while(!quit_) {
        if(can_produce()) {
            try {
                for(unsigned budget=0;queued()<stats_.target_bytes && budget<timing::producer_batch_limit;++budget) {
                    const auto start=SDL_GetTicksNS();
                    const auto before=machine_->state().tick;
                    if(!scheduled_buttons_ || buttons_!=*scheduled_buttons_) {
                        machine_->schedule_input(before+1,buttons_);
                        scheduled_buttons_=buttons_;
                    }
                    const auto frame=machine_->frame().number;
                    // Audio consumption paces requests; execute every tick.
                    machine_->advance_to(before+timing::producer_chunk_ticks);
                    const auto pcm=machine_->take_audio();
                    if(!SDL_PutAudioStreamData(stream_,pcm.data(),int(pcm.size()*timing::sample_bytes)))
                        throw std::runtime_error(SDL_GetError());
                    stats_.samples+=pcm.size();
                    stats_.ticks+=timing::producer_chunk_ticks;
                    if(machine_->frame().number!=frame) {
                        latest_=machine_->frame();
                        ++stats_.frames;
                        stats_.peak_video=1;
                    }
                    stats_.max_chunk_us=std::max(stats_.max_chunk_us,(SDL_GetTicksNS()-start)/1000);
                }
                const auto waiting=queued();
                stats_.peak_audio=std::max(stats_.peak_audio,waiting);
                // Share one latest completed frame, never a FIFO of stale
                // frames. Its lead is bounded by the short source queue.
                ready_=ready_ || waiting>=stats_.target_bytes;
            }catch(const std::exception& e) {
                error_=e.what();
                active_=false;
            }
        }
        if(!can_produce())wake_.wait(lock,[&]{return quit_ || can_produce();});
        else wake_.wait_for(lock,timing::producer_poll_interval);
    }
}
}
