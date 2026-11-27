// SPDX-License-Identifier: Apache-2.0
#include "smoke.hpp"
#include "display.hpp"
#include <rse/systems/nes/session.hpp>
#include <rse/systems/nes/palette.hpp>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace rse::desktop {
std::optional<std::uint8_t> smoke_memory(const session::Session& machine,std::uint16_t address) {
    const auto* nes=dynamic_cast<const nes::Session*>(&machine);
    return nes?nes->diagnostics().inspect_memory(address):std::nullopt;
}
namespace {
constexpr std::uint64_t first_stage_ms=500,stage_interval_ms=150,blocked_gui_ms=150;
constexpr unsigned final_stage=16;
constexpr unsigned gba_final_stage=26;
constexpr std::size_t response_pixel=32000;
constexpr std::uint64_t minimum_stall_samples=4800;
void expect(bool ok,const char* reason) {if(!ok)throw std::runtime_error(reason);}
}

void SmokeMetrics::record(std::uint64_t latency) {
    if(!responses)first_us=min_us=latency;
    min_us=std::min(min_us,latency);
    max_us=std::max(max_us,latency);
    sum_us+=latency;
    ++responses;
}
void SmokeMetrics::report(std::ostream& output) const {
    output<<" input_frame_latency_us="<<first_us
          <<" input_responses="<<responses<<" input_min_us="<<min_us
          <<" input_max_us="<<max_us<<" input_mean_us="<<(responses?sum_us/responses:0);
}
Smoke::Smoke(std::filesystem::path fixture,bool gba):path_(std::move(fixture)),gba_(gba) {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
    desc.naxes=SDL_GAMEPAD_AXIS_COUNT;
    desc.button_mask=0xffffffff;
    desc.axis_mask=0x3f;
    desc.name="RSE smoke gamepad";
    id_=SDL_AttachVirtualJoystick(&desc);
    require_sdl(id_!=0);
    joystick_.reset(SDL_OpenJoystick(id_));
    if(!joystick_) {
        const std::string error=SDL_GetError();
        SDL_DetachVirtualJoystick(id_);
        throw std::runtime_error(error);
    }
}
Smoke::~Smoke() {
    joystick_.reset();
    if(id_)SDL_DetachVirtualJoystick(id_);
}
void Smoke::response(std::uint16_t sample) {
    expected_sample_=sample;
    input_start_=SDL_GetTicksNS();
}
void Smoke::tick(SmokeHost& host,std::uint64_t elapsed) {
    if(gba_) {tick_gba(host,elapsed);return;}
    const auto state=host.observation();
    // Sessions have no published frame during initial load/reset. The overall
    // duration/final-stage checks still fail if publication never resumes.
    if(state.frame_samples.empty())return;
    expect(state.frame_samples.size()>response_pixel,"smoke frame dimensions changed");
    if(input_start_ && state.frame_samples[response_pixel]==nes::display_color(expected_sample_)) {
        metrics_.record((SDL_GetTicksNS()-input_start_)/1000);
        input_start_=0;
    }
    if(elapsed<first_stage_ms+stage_*stage_interval_ms)return;
    if(stage_<=final_stage)std::cerr<<"Smoke step "<<stage_<<'\n';
    auto push=[](SDL_Event e){require_sdl(SDL_PushEvent(&e));};
    auto key=[&](SDL_Scancode code,bool down) {
        SDL_Event e{};e.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;e.key.scancode=code;push(e);
    };
    auto focus=[&](bool gained) {
        SDL_Event e{};e.type=gained?SDL_EVENT_WINDOW_FOCUS_GAINED:SDL_EVENT_WINDOW_FOCUS_LOST;push(e);
    };
    auto drop=[&](std::string filename) {
        drop_=std::move(filename);
        SDL_Event e{};e.type=SDL_EVENT_DROP_FILE;e.drop.data=drop_.c_str();push(e);
    };
    switch(stage_++) {
    case 0:focus(true);key(SDL_SCANCODE_X,true);response(0x20);break;
    case 1:
        expect(state.buttons==1,"keyboard input failed");
        expect(host.inspect_memory(0x10)==1,"keyboard did not reach NROM controller reader");
        focus(false);response(0x21);break;
    case 2:expect(state.buttons==0,"focus did not release keyboard");focus(true);break;
    case 3:
        expect(state.buttons==0,"keyboard stuck after focus regained");
        host.select_gamepad(id_);
        require_sdl(SDL_SetJoystickVirtualButton(joystick_.get(),SDL_GAMEPAD_BUTTON_SOUTH,true));
        response(0x20);break;
    case 4:
        expect(state.buttons==1,"gamepad mapping failed");
        expect(host.inspect_memory(0x10)==1,"gamepad did not reach NROM controller reader");
        require_sdl(SDL_SetJoystickVirtualButton(joystick_.get(),SDL_GAMEPAD_BUTTON_SOUTH,false));
        response(0x21);break;
    case 5:expect(state.buttons==0,"gamepad release failed");key(SDL_SCANCODE_F5,true);break;
    case 6:
        expect(state.resets==1 && state.tick<20'000'000,"reset event failed");
        expect(state.frame_number>0 && state.frame_samples[response_pixel]!=nes::display_color(0),"smoke did not present an emulated color frame");
        host.resize_window(1000,480);host.request_readback();break;
    case 7:host.request_readback();host.resize_window(480,720);break;
    case 8:host.request_readback();drop("rse-nonexistent-smoke-fixture.nes");break;
    case 9:
        expect(state.errors==1 && state.loaded,"invalid cartridge did not preserve running machine");
        drop(path_.string());break;
    case 10:
        expect(state.loads==2,"drop-file replacement failed");
        host.request_readback();host.minimize_window(true);break;
    case 11:host.minimize_window(false);break;
    case 12:host.request_readback();break;
    case 13:drop(path_.string()+".unsupported.nes");break;
    case 14:
        expect(state.errors==2 && state.loaded,"unsupported cartridge recovery failed");
        drop(path_.string()+".invalid.nes");break;
    case 15:expect(state.errors==3 && state.loaded,"malformed cartridge recovery failed");break;
    case 16:
        SDL_Delay(blocked_gui_ms);
        expect(host.observation().samples>state.samples+minimum_stall_samples,"audio producer stopped during a blocked GUI");break;
    default:break;
    }
}

void Smoke::tick_gba(SmokeHost& host,std::uint64_t elapsed) {
    const auto state=host.observation();
    if (state.frame_samples.empty() || elapsed < next_gba_stage_ || stage_ > gba_final_stage) return;
    expect(state.format.width==240 && state.format.height==160 && state.frame_samples.size()==240*160 &&
           state.format.display_aspect==session::Rational{3,2},"GBA frame dimensions or display aspect");
    expect(state.frame_samples.back()==session::Rgba{132,132,132,255},"GBA authored backdrop was not published");
    next_gba_stage_=elapsed+120;
    constexpr SDL_Scancode keyboard[]{SDL_SCANCODE_X,SDL_SCANCODE_Z,SDL_SCANCODE_RSHIFT,SDL_SCANCODE_RETURN,
        SDL_SCANCODE_UP,SDL_SCANCODE_DOWN,SDL_SCANCODE_LEFT,SDL_SCANCODE_RIGHT,SDL_SCANCODE_Q,SDL_SCANCODE_W};
    constexpr SDL_GamepadButton gamepad[]{SDL_GAMEPAD_BUTTON_SOUTH,SDL_GAMEPAD_BUTTON_WEST,
        SDL_GAMEPAD_BUTTON_BACK,SDL_GAMEPAD_BUTTON_START,SDL_GAMEPAD_BUTTON_DPAD_UP,SDL_GAMEPAD_BUTTON_DPAD_DOWN,
        SDL_GAMEPAD_BUTTON_DPAD_LEFT,SDL_GAMEPAD_BUTTON_DPAD_RIGHT,SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
        SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER};
    constexpr unsigned keyinput_bits[]{0,1,2,3,6,7,5,4,9,8};
    const auto key=[&](SDL_Scancode code,bool down) {
        SDL_Event event{};
        event.type=down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.scancode=code;
        require_sdl(SDL_PushEvent(&event));
    };
    const auto button=[&](unsigned index,bool down) {
        require_sdl(SDL_SetJoystickVirtualButton(joystick_.get(),gamepad[index],down));
    };
    const auto verify=[&](unsigned mask,unsigned pressed) {
        const auto pixel=state.frame_samples.front();
        const unsigned word=(pixel.r>>3)|((pixel.g>>3)<<5)|((pixel.b>>3)<<10);
        expect(state.buttons==mask && word==(0x3ff ^ pressed),"GBA desktop binding did not reach KEYINPUT");
    };
    const auto step=stage_++;
    if(step==0) {
        SDL_Event focus{};focus.type=SDL_EVENT_WINDOW_FOCUS_GAINED;
        require_sdl(SDL_PushEvent(&focus));
        host.resize_window(1000,480);host.request_readback();
        key(keyboard[0],true);
    } else if(step<=10) {
        verify(1u<<(step-1),1u<<keyinput_bits[step-1]);
        key(keyboard[step-1],false);
        if(step<10) key(keyboard[step],true);
    } else if(step==11) {
        verify(0,0);
        host.select_gamepad(id_);
        button(0,true);
    } else if(step<=21) {
        const unsigned previous=step-12;
        verify(1u<<previous,1u<<keyinput_bits[previous]);
        button(previous,false);
        if(previous<9) button(previous+1,true);
    } else if(step==22) {
        verify(0,0);
        expect(state.audio_running && state.samples>0,"GBA audio did not start");
        key(SDL_SCANCODE_P,true);
    } else if(step==23) {
        expect(!state.audio_running,"manual pause left audio running");
        paused_tick_=state.tick;
        paused_samples_=state.samples;
        key(SDL_SCANCODE_P,false);
    } else if(step==24) {
        expect(state.tick==paused_tick_ && state.samples==paused_samples_,"paused GBA timeline advanced");
        key(SDL_SCANCODE_P,true);
    } else if(step==25) {
        expect(state.audio_running && state.tick>paused_tick_ && state.samples>paused_samples_,
               "GBA audio or frame timeline did not resume");
        before_reset_frame_=state.frame_number;
        key(SDL_SCANCODE_P,false);
        key(SDL_SCANCODE_F5,true);
    } else {
        verify(0,0);
        expect(state.resets==1 && state.frame_number>0 && state.frame_number<before_reset_frame_ &&
               state.audio_running && state.samples>0,"GBA reset did not recover frames, input and audio");
        host.request_readback();
    }
    if ((step>=1 && step<=10) || (step>=12 && step<=21)) ++metrics_.responses;
}

void Smoke::finish(SmokeHost& host) {
    const auto state=host.observation();
    if(stage_<=(gba_ ? gba_final_stage : final_stage) || state.readbacks<(gba_ ? 2u : 3u) ||
       state.starvation || metrics_.responses!=(gba_ ? 20u : 4u))
        throw std::runtime_error("desktop smoke acceptance failed: stage="+std::to_string(stage_)+
            " readbacks="+std::to_string(state.readbacks)+" starvation="+std::to_string(state.starvation));
    SDL_Event e{};e.type=SDL_EVENT_WINDOW_CLOSE_REQUESTED;require_sdl(SDL_PushEvent(&e));host.pump();
    expect(!host.observation().running,"window close failed");
}
}
