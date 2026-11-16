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
Smoke::Smoke(std::filesystem::path fixture):path_(std::move(fixture)) {
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
void Smoke::finish(SmokeHost& host) {
    const auto state=host.observation();
    if(stage_<=final_stage || state.readbacks<3 || state.starvation || metrics_.responses!=4)
        throw std::runtime_error("desktop smoke acceptance failed: stage="+std::to_string(stage_)+
            " readbacks="+std::to_string(state.readbacks)+" starvation="+std::to_string(state.starvation));
    SDL_Event e{};e.type=SDL_EVENT_WINDOW_CLOSE_REQUESTED;require_sdl(SDL_PushEvent(&e));host.pump();
    expect(!host.observation().running,"window close failed");
}
}
