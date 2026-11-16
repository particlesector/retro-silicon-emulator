// SPDX-License-Identifier: Apache-2.0
#include <rse/systems/nes/session.hpp>
#include <rse/systems/nes/palette.hpp>
#include <algorithm>
#include <array>

namespace rse::nes {
namespace {
constexpr std::array<std::string_view,8> actions{"a","b","select","start","up","down","left","right"};
constexpr std::array<session::InputPort,1> ports{{{"controller-1",actions}}};
Profile capture(Profile profile) {profile.capture_audio=true;return profile;}
}
Session::Session(Cartridge cartridge,Profile profile):machine_(std::move(cartridge),capture(profile)),info_{
    "nes-ntsc","NTSC NES (NROM)",{236'250'000,11},
    session::VideoFormat{256,240,{4,3},session::PixelFormat::indexed_u16_native},
    session::AudioFormat{48'000,1,session::SampleFormat::signed16_native,profile.audio_limit},ports,true} {}
session::Status Session::status() const noexcept {
    const auto state=machine_.state();
    return {state.tick,state.ppu.frames,state.faulted};
}
void Session::reset() {machine_.reset();}
void Session::advance_to(Tick target) {machine_.advance_to(target);}
void Session::run_frame() {(void)machine_.run_frame();}
void Session::schedule_input(Tick tick,std::string_view port,std::span<const std::string_view> pressed) {
    if(port!=ports[0].id)throw std::invalid_argument("Unknown NES input port");
    std::uint8_t mask=0;
    for(const auto action:pressed) {
        const auto found=std::find(actions.begin(),actions.end(),action);
        if(found==actions.end())throw std::invalid_argument("Unknown NES input action");
        const auto bit=std::uint8_t(1u<<std::distance(actions.begin(),found));
        if(mask&bit)throw std::invalid_argument("Duplicate NES input action");
        mask|=bit;
    }
    machine_.schedule_input(tick,mask);
}
std::optional<session::VideoFrame> Session::video() const noexcept {
    const auto& frame=machine_.frame();
    if(!frame.number)return std::nullopt;
    return session::VideoFrame{frame.number,rse::video::PpuFrame::width*sizeof(std::uint16_t),
        std::as_bytes(std::span(frame.samples))};
}
void Session::convert_video(std::span<session::Rgba> output) const {
    if(output.size()!=rse::video::PpuFrame::width*rse::video::PpuFrame::height)
        throw std::invalid_argument("NES RGBA output size must match its video dimensions");
    if(!machine_.frame().number)throw std::logic_error("No published NES video frame");
    const auto& samples=machine_.frame().samples;
    std::transform(samples.begin(),samples.end(),output.begin(),display_color);
}
std::vector<std::int16_t> Session::take_audio() {return machine_.take_audio();}
}
