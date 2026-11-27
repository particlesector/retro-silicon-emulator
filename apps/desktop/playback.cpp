// SPDX-License-Identifier: Apache-2.0
#include "playback.hpp"
#include "playback_timing.hpp"
#include <rse/session/time.hpp>
#include <limits>
#include <algorithm>
#include <stdexcept>

namespace rse::desktop {
Playback::Playback(SDL_AudioStream* stream):stream_(stream) {
    if(stream_) {
        SDL_AudioSpec device{};
        int frames{};
        if(!SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream),&device,&frames))
            throw std::runtime_error(SDL_GetError());
        if(device.freq<=0 || frames<=0)
            throw std::runtime_error("Audio device reported an invalid period");
        // One negotiated device request plus the existing 2 ms source margin.
        device_rate_=unsigned(device.freq);device_frames_=unsigned(frames);
    }
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
    next_silent_ns_=0;
    inputs_.clear();
    scheduled_inputs_.reset();
    error_.clear();
}
void Playback::replace(std::unique_ptr<session::Session> machine) {
    std::lock_guard lock(mutex_);
    if(!machine)throw std::invalid_argument("Missing session");
    const auto& info=machine->info();
    if(info.video && (!info.video->width || !info.video->height ||
       info.video->width>16384 || info.video->height>16384 ||
       !info.video->display_aspect.numerator || !info.video->display_aspect.denominator))
        throw std::invalid_argument("Unsupported session video dimensions or aspect");
    const auto chunk=session::ticks_for_us(info.clock_hz,timing::producer_chunk_us);
    const auto silent_period=std::ceil(static_cast<long double>(chunk)*info.clock_hz.denominator*1'000'000'000/info.clock_hz.numerator);
    if(silent_period>=static_cast<long double>(std::numeric_limits<std::uint64_t>::max()))
        throw std::invalid_argument("Unsupported session clock period");
    unsigned target=0;
    if(info.audio) {
        if(!stream_)throw session::UnsupportedOperation("Session requires an audio device");
        const auto& format=*info.audio;
        if(!format.sample_rate || !format.channels || format.channels>8 ||
           format.sample_rate>384000 || format.samples!=session::SampleFormat::signed16_native)
            throw std::invalid_argument("Unsupported session audio format");
        const auto period=(std::uint64_t(device_frames_)*format.sample_rate+device_rate_-1)/device_rate_;
        const auto margin=(std::uint64_t(format.sample_rate)*timing::source_margin_ms+999)/1000;
        target=unsigned((period+margin)*format.channels*sizeof(std::int16_t));
        SDL_AudioSpec spec{SDL_AUDIO_S16,int(format.channels),int(format.sample_rate)};
        if(!SDL_SetAudioStreamFormat(stream_,&spec,nullptr))throw std::runtime_error(SDL_GetError());
    }
    machine_=std::move(machine);chunk_ticks_=chunk;stats_.target_bytes=target;
    silent_period_ns_=static_cast<std::uint64_t>(silent_period);
    reset_timeline();
}
void Playback::reset() {
    std::lock_guard lock(mutex_);
    if(machine_)machine_->reset();
    reset_timeline();
}
void Playback::flush_save() {
    std::lock_guard lock(mutex_);
    if(machine_)machine_->flush_save();
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
void Playback::input(std::vector<PortInput> value) {
    std::lock_guard lock(mutex_);
    if(!machine_)return;
    const auto ports=machine_->info().input_ports;
    for(std::size_t i=0;i<value.size();++i) {
        const auto& input=value[i];
        const auto port=std::find_if(ports.begin(),ports.end(),[&](const auto& item){return item.id==input.port;});
        if(port==ports.end())throw std::invalid_argument("Unknown input port");
        for(std::size_t j=0;j<i;++j)if(value[j].port==input.port)throw std::invalid_argument("Duplicate input port");
        for(std::size_t j=0;j<input.pressed.size();++j) {
            if(std::find(port->actions.begin(),port->actions.end(),input.pressed[j])==port->actions.end())
                throw std::invalid_argument("Unknown input action");
            for(std::size_t k=0;k<j;++k)if(input.pressed[k]==input.pressed[j])throw std::invalid_argument("Duplicate input action");
        }
    }
    inputs_=std::move(value);
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
    return machine_?machine_->status().tick:0;
}
session::Status Playback::state() const {
    std::lock_guard lock(mutex_);
    return machine_?machine_->status():session::Status{};
}
bool Playback::has_audio() const {
    std::lock_guard lock(mutex_);return machine_ && machine_->info().audio.has_value();
}
bool Playback::inspect(const std::function<bool(const session::Session&)>& query) const {
    std::lock_guard lock(mutex_);return machine_ && query(*machine_);
}
std::optional<std::uint64_t> Playback::peek(std::string_view id,std::uint32_t address) const {
    std::lock_guard lock(mutex_);
    auto* inspection=machine_ ? machine_->inspection() : nullptr;
    if(!inspection) return std::nullopt;
    try {return inspection->peek(id,address);}
    catch(const std::exception&) {return std::nullopt;}
}
bool Playback::update_frame(Frame& frame) const {
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
    if(!stream_)return 0;
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
                for(unsigned budget=0;(machine_->info().audio?queued()<stats_.target_bytes:budget==0 && SDL_GetTicksNS()>=next_silent_ns_) && budget<timing::producer_batch_limit;++budget) {
                    const auto start=SDL_GetTicksNS();
                    const auto before=machine_->status().tick;
                    if(!scheduled_inputs_ || inputs_!=*scheduled_inputs_) {
                        for(const auto& port:machine_->info().input_ports) {
                            const auto input=std::find_if(inputs_.begin(),inputs_.end(),[&](const auto& item){return item.port==port.id;});
                            std::vector<std::string_view> actions;
                            if(input!=inputs_.end())actions.assign(input->pressed.begin(),input->pressed.end());
                            machine_->schedule_input(before+1,port.id,actions);
                        }
                        scheduled_inputs_=inputs_;
                    }
                    const auto frame=machine_->status().video_frames;
                    // Audio consumption paces requests; execute every tick.
                    if(chunk_ticks_>std::numeric_limits<Tick>::max()-before)throw std::overflow_error("Playback clock overflow");
                    machine_->advance_to(before+chunk_ticks_);
                    if(!machine_->info().audio)next_silent_ns_=SDL_GetTicksNS()+silent_period_ns_;
                    const auto pcm=machine_->info().audio?machine_->take_audio():std::vector<std::int16_t>{};
                    if(!pcm.empty() && !SDL_PutAudioStreamData(stream_,pcm.data(),int(pcm.size()*timing::sample_bytes)))
                        throw std::runtime_error(SDL_GetError());
                    stats_.samples+=pcm.size();
                    stats_.ticks+=chunk_ticks_;
                    const auto clock=machine_->info().clock_hz;
                    simulated_ms_+=static_cast<long double>(chunk_ticks_)*clock.denominator*1000/clock.numerator;
                    stats_.simulated_ms=static_cast<std::uint64_t>(simulated_ms_);
                    if(machine_->status().video_frames!=frame && machine_->info().video && machine_->video()) {
                        const auto& format=*machine_->info().video;
                        latest_.format=format;latest_.number=machine_->video()->number;
                        latest_.format.pixels=session::PixelFormat::rgba8;
                        latest_.pixels.resize(std::size_t(format.width)*format.height);
                        machine_->convert_video(latest_.pixels);
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
