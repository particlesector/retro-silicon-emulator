// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <rse/video/rp2c02.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>

namespace rse::desktop {
struct Rgb {std::uint8_t r{},g{},b{},a{255};bool operator==(const Rgb&) const=default;};
// Project-owned approximate display palette. Integer hue/luma construction;
// not a measured 2C02/composite decoder. Emphasis attenuates other primaries.
inline Rgb rgb(std::uint16_t sample) noexcept {
    constexpr std::array<std::array<int,3>,12> hues{{
        {{-64,-32,96}},{{0,-64,96}},{{64,-64,64}},{{96,-64,0}},
        {{96,-32,-64}},{{64,0,-96}},{{0,32,-96}},{{-64,64,-64}},
        {{-96,64,0}},{{-96,32,64}},{{-64,0,96}},{{-32,-32,96}}}};
    constexpr int luma[]{72,128,184,224};
    const unsigned color=sample&63,level=color>>4,hue=color&15;
    int r=0,g=0,b=0;
    if (hue==0) r=g=b=luma[level]+24;
    else if (hue<=12) {r=luma[level]+hues[hue-1][0];g=luma[level]+hues[hue-1][1];b=luma[level]+hues[hue-1][2];}
    else if (hue==13 && level>=2) r=g=b=luma[level]-80;
    r=std::clamp(r,0,255);g=std::clamp(g,0,255);b=std::clamp(b,0,255);
    if (sample&64) {g=g*3/4;b=b*3/4;}
    if (sample&128) {r=r*3/4;b=b*3/4;}
    if (sample&256) {r=r*3/4;g=g*3/4;}
    return {std::uint8_t(r),std::uint8_t(g),std::uint8_t(b),255};
}
inline auto pixels(const video::PpuFrame& frame) noexcept {
    std::array<Rgb,video::PpuFrame::width*video::PpuFrame::height> output;
    std::transform(frame.samples.begin(),frame.samples.end(),output.begin(),rgb);return output;
}
struct Viewport {int x{},y{},w{},h{};bool operator==(const Viewport&) const=default;};
inline Viewport viewport(int width,int height) noexcept {
    if (width<=0 || height<=0) return {};
    const auto w=std::min<std::int64_t>(width,std::int64_t(height)*4/3);
    const auto h=w*3/4;return {(width-int(w))/2,(height-int(h))/2,int(w),int(h)};
}
class Input {
public:
    void key(std::uint8_t mask,bool down) noexcept {
        if (!focused_) return;
        if (down) keys_|=mask;else keys_&=std::uint8_t(~mask);
    }
    void gamepad(std::uint8_t buttons) noexcept {pad_=buttons;}
    void focus(bool value) noexcept {focused_=value;if (!value) keys_=0;}
    void clear() noexcept {keys_=pad_=0;}
    [[nodiscard]] std::uint8_t buttons() const noexcept {return focused_ ? keys_|pad_ : 0;}
private:
    std::uint8_t keys_{},pad_{};bool focused_{true};
};
struct VideoPacket {video::PpuFrame frame;std::uint64_t sample_end{};};
// Frames become visible at their publication time on the consumed PCM timeline.
// Pending packets are bounded by the host audio queue, not wall-clock frame rate.
class VideoTimeline {
public:
    void push(const video::PpuFrame& frame,std::uint64_t sample_end) {pending_.push_back({frame,sample_end});}
    bool advance(std::uint64_t consumed) {
        bool changed=false;
        while (!pending_.empty() && pending_.front().sample_end<=consumed) {
            visible_=std::move(pending_.front().frame);pending_.pop_front();changed=true;
        }
        return changed;
    }
    void reset() {pending_.clear();visible_={};}
    [[nodiscard]] const video::PpuFrame& visible() const noexcept {return visible_;}
    [[nodiscard]] std::size_t queued() const noexcept {return pending_.size();}
private:
    video::PpuFrame visible_;std::deque<VideoPacket> pending_;
};
} // namespace rse::desktop
