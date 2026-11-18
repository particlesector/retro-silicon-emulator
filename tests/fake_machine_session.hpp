// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <rse/session/session.hpp>
#include <algorithm>
#include <array>

namespace rse::testing {
// Test-only system with non-NES clock, video size, aspect and stereo PCM.
class FakeMachineSession final : public session::Session {
public:
    explicit FakeMachineSession(bool video=true,bool audio=true,bool fault=false)
        :info_{"test-only","Test only",{1'000'000,1},
            video?std::optional{session::VideoFormat{3,2,{3,2},session::PixelFormat::rgba8}}:std::nullopt,
            audio?std::optional{session::AudioFormat{32'000,2,session::SampleFormat::signed16_native,64000}}:std::nullopt,
            ports_,video},fault_(fault) {}
    const session::SystemInfo& info() const noexcept override {return info_;}
    session::Status status() const noexcept override {return state_;}
    void reset() override {state_={};audio_.clear();events_.clear();pressed_=false;pixels_={};}
    void advance_to(Tick target) override {
        if(state_.faulted)throw std::logic_error("Reset required");
        if(target<state_.tick)throw std::invalid_argument("Rewind");
        if(target==state_.tick)return;
        if(fault_){state_.faulted=true;throw std::runtime_error("Test failure");}
        while(state_.tick<target) {
            ++state_.tick;
            while(!events_.empty() && events_.front().first<=state_.tick) {
                pressed_=events_.front().second;events_.erase(events_.begin());
            }
            if(info_.audio && state_.tick%125==0) {
                if(audio_.size()+8>info_.audio->max_buffered_samples){state_.faulted=true;throw std::length_error("Test PCM bound");}
                audio_.insert(audio_.end(),8,1234);
            }
            if(info_.video && state_.tick%1000==0) {
                ++state_.video_frames;
                pixels_.fill({std::uint8_t(pressed_?255:17),33,65,255});
            }
        }
    }
    void run_frame() override {
        if(!info_.frame_step)return session::Session::run_frame();
        advance_to((state_.tick/1000+1)*1000);
    }
    void schedule_input(Tick tick,std::string_view port,std::span<const std::string_view> actions) override {
        if(state_.faulted)throw std::logic_error("Reset required");
        if(tick<=state_.tick || port!="panel" || actions.size()>1 || (!actions.empty() && actions[0]!="fire"))
            throw std::invalid_argument("Invalid panel input");
        const auto at=std::upper_bound(events_.begin(),events_.end(),tick,[](Tick time,const auto& event){return time<event.first;});
        events_.insert(at,{tick,!actions.empty()});
    }
    bool pressed() const {return pressed_;}
    std::optional<session::VideoFrame> video() const noexcept override {
        if(!state_.video_frames)return std::nullopt;
        return session::VideoFrame{state_.video_frames,12,std::as_bytes(std::span(pixels_))};
    }
    void convert_video(std::span<session::Rgba> output) const override {
        if(!info_.video)return session::Session::convert_video(output);
        if(output.size()!=6)throw std::invalid_argument("Panel dimensions");
        if(!state_.video_frames)throw std::logic_error("No published frame");
        std::copy(pixels_.begin(),pixels_.end(),output.begin());
    }
    std::vector<std::int16_t> take_audio() override {
        if(!info_.audio)return session::Session::take_audio();
        auto result=std::move(audio_);audio_.clear();return result;
    }
private:
    inline static constexpr std::array<std::string_view,1> actions_{"fire"};
    inline static constexpr std::array<session::InputPort,1> ports_{{{"panel",actions_}}};
    session::SystemInfo info_;
    session::Status state_;
    std::array<session::Rgba,6> pixels_{};
    std::vector<std::int16_t> audio_;
    std::vector<std::pair<Tick,bool>> events_;
    bool fault_{},pressed_{};
};
}
