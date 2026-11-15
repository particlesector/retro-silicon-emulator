// SPDX-License-Identifier: Apache-2.0
#include "playback.hpp"
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace rse::desktop {
Playback::Playback(SDL_AudioStream* stream):stream_(stream) {
    SDL_AudioSpec device{};int frames{};
    if(!SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream),&device,&frames))
        throw std::runtime_error(SDL_GetError());
    if(device.freq<=0 || frames<=0)throw std::runtime_error("Audio device reported an invalid period");
    // One actual device request plus 2 ms of scheduling margin, in source bytes.
    // The requested device period is only a hint; use the negotiated format.
    stats_.target_bytes=unsigned((std::uint64_t(frames)*48000+device.freq-1)/device.freq+96)*2;
    worker_=std::thread([this]{produce();});
}
Playback::~Playback() {
    {std::lock_guard lock(mutex_);quit_=true;}wake_.notify_one();worker_.join();
}
void Playback::reset_timeline() {
    latest_={};ready_=false;buttons_=0;scheduled_buttons_=255;error_.clear();
}
void Playback::replace(std::unique_ptr<nes::Machine> machine) {
    std::lock_guard lock(mutex_);machine_=std::move(machine);reset_timeline();
}
void Playback::reset() {std::lock_guard lock(mutex_);if(machine_)machine_->reset();reset_timeline();}
void Playback::clear() {std::lock_guard lock(mutex_);machine_.reset();reset_timeline();}
void Playback::active(bool value) {{std::lock_guard lock(mutex_);active_=value;if(!value)ready_=false;}wake_.notify_one();}
void Playback::buttons(std::uint8_t value) {std::lock_guard lock(mutex_);buttons_=value;}
bool Playback::loaded() const {std::lock_guard lock(mutex_);return bool(machine_);}
bool Playback::ready() const {std::lock_guard lock(mutex_);return ready_;}
Tick Playback::tick() const {std::lock_guard lock(mutex_);return machine_?machine_->state().tick:0;}
nes::MachineState Playback::state() const {std::lock_guard lock(mutex_);return machine_?machine_->state():nes::MachineState{};}
std::optional<std::uint8_t> Playback::memory(std::uint16_t address) const {
    std::lock_guard lock(mutex_);return machine_?machine_->inspect_memory(address):std::nullopt;
}
bool Playback::update_frame(video::PpuFrame& frame) const {
    std::lock_guard lock(mutex_);
    if(frame.number==latest_.number)return false;
    frame=latest_;return true;
}
PlaybackStats Playback::stats() const {std::lock_guard lock(mutex_);return stats_;}
std::string Playback::error() const {std::lock_guard lock(mutex_);return error_;}
unsigned Playback::queued() const {
    const int count=SDL_GetAudioStreamQueued(stream_);
    if(count<0)throw std::runtime_error(SDL_GetError());return unsigned(count);
}
void Playback::produce() {
    std::unique_lock lock(mutex_);
#ifdef SDL_PLATFORM_WINDOWS
    stats_.high_priority=SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH);
#endif
    while(!quit_) {
        if(active_ && machine_ && error_.empty()) {
            try {
                for(unsigned budget=0;queued()<stats_.target_bytes && budget<32;++budget) {
                    const auto start=SDL_GetTicksNS(),before=machine_->state().tick;
                    if(buttons_!=scheduled_buttons_) {
                        machine_->schedule_input(before+1,buttons_);scheduled_buttons_=buttons_;
                    }
                    const auto frame=machine_->frame().number;
                    // About 1 ms of NTSC master ticks. Audio consumption alone
                    // paces requests; every simulated tick is still executed.
                    machine_->advance_to(before+21480);
                    const auto pcm=machine_->take_audio();
                    if(!SDL_PutAudioStreamData(stream_,pcm.data(),int(pcm.size()*2)))throw std::runtime_error(SDL_GetError());
                    stats_.samples+=pcm.size();stats_.ticks+=21480;
                    if(machine_->frame().number!=frame) {latest_=machine_->frame();++stats_.frames;stats_.peak_video=1;}
                    stats_.max_chunk_us=std::max(stats_.max_chunk_us,(SDL_GetTicksNS()-start)/1000);
                }
                const auto waiting=queued();stats_.peak_audio=std::max(stats_.peak_audio,waiting);
                // The newest completed frame is shared with the GUI, never a
                // FIFO of stale frames. Its lead is bounded by the source queue.
                ready_=ready_ || waiting>=stats_.target_bytes;
            }catch(const std::exception& e){error_=e.what();active_=false;}
        }
        if(!active_ || !machine_ || !error_.empty())wake_.wait(lock,[&]{return quit_ || (active_ && machine_ && error_.empty());});
        else wake_.wait_for(lock,std::chrono::milliseconds(1));
    }
}
}
