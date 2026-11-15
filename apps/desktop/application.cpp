// SPDX-License-Identifier: Apache-2.0
#include "application.hpp"
#include "display.hpp"
#include "playback.hpp"
#include <rse/systems/nes/machine.hpp>
#include <atomic>
#include <iostream>
#include <mutex>
#include <optional>

namespace rse::desktop {
namespace {
struct DialogResult {std::mutex mutex;bool pending{},ready{};std::string path,error;};
void SDLCALL selected(void* userdata,const char* const* files,int) {
    std::unique_ptr<std::shared_ptr<DialogResult>> token(static_cast<std::shared_ptr<DialogResult>*>(userdata));
    auto& result=**token;std::lock_guard lock(result.mutex);
    if (!files) result.error=SDL_GetError();else if (files[0]) result.path=files[0];
    result.pending=false;result.ready=true;
}
std::uint8_t key_mask(SDL_Scancode key) {
    switch (key) {
    case SDL_SCANCODE_X:return 1;case SDL_SCANCODE_Z:return 2;
    case SDL_SCANCODE_RSHIFT:return 4;case SDL_SCANCODE_RETURN:return 8;
    case SDL_SCANCODE_UP:return 16;case SDL_SCANCODE_DOWN:return 32;
    case SDL_SCANCODE_LEFT:return 64;case SDL_SCANCODE_RIGHT:return 128;
    default:return 0;
    }
}
class Application {
public:
    explicit Application(bool test) : smoke(test) {
        window.reset(SDL_CreateWindow("Retro Silicon Emulator",960,720,SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY));
        require_sdl(bool(window));display=std::make_unique<Display>(window.get());
        SDL_AudioSpec spec{SDL_AUDIO_S16,1,48000};
        audio.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,observe,this));
        if (!audio) throw std::runtime_error(std::string("Audio device unavailable: ")+SDL_GetError());
        playback=std::make_unique<Playback>(audio.get());
        int count{};auto* ids=SDL_GetGamepads(&count);
        for (int i=0;i<count && !pad;++i) connect(ids[i]);SDL_free(ids);
    }
    void load(const std::filesystem::path& path) {
        try {
            auto next=std::make_unique<nes::Machine>(nes::Cartridge::load_file(path),nes::Profile{.capture_audio=true});
            const auto filename=path.filename().u8string();
            const auto title=std::string("Retro Silicon Emulator - ")+std::string(reinterpret_cast<const char*>(filename.data()),filename.size());
            stop_audio();playback->replace(std::move(next));input.clear();visible={};running_audio=false;
            (void)SDL_SetWindowTitle(window.get(),title.c_str());redraw=true;++loads;
        } catch (const std::exception& e) {error(std::string("Cannot open cartridge: ")+e.what()+"\nChoose a supported mapper-0 iNES image. The previous cartridge is retained.");}
    }
    void reset() {
        if (!playback->loaded()) return;
        stop_audio();playback->reset();input.clear();visible={};running_audio=false;redraw=true;++resets;
    }
    void connect(SDL_JoystickID id) {if (!pad && SDL_IsGamepad(id)) pad.reset(SDL_OpenGamepad(id));}
    void event(const SDL_Event& e) {
        if (e.type==SDL_EVENT_QUIT || e.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED) {running=false;return;}
        if (e.type==SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || e.type==SDL_EVENT_WINDOW_EXPOSED) redraw=true;
        if (e.type==SDL_EVENT_WINDOW_FOCUS_LOST) {input.focus(false);return;}
        if (e.type==SDL_EVENT_WINDOW_FOCUS_GAINED) {input.focus(true);return;}
        if (e.type==SDL_EVENT_WINDOW_MINIMIZED) {minimized=true;pause_audio();return;}
        if (e.type==SDL_EVENT_WINDOW_RESTORED) {minimized=false;redraw=true;return;}
        if (e.type==SDL_EVENT_DROP_FILE && e.drop.data) {load(utf8_path(e.drop.data));return;}
        if (e.type==SDL_EVENT_GAMEPAD_ADDED) {connect(e.gdevice.which);return;}
        if (e.type==SDL_EVENT_GAMEPAD_REMOVED && pad && SDL_GetGamepadID(pad.get())==e.gdevice.which) {pad.reset();input.gamepad(0);return;}
        if (e.type==SDL_EVENT_KEY_DOWN || e.type==SDL_EVENT_KEY_UP) {
            const bool down=e.type==SDL_EVENT_KEY_DOWN;input.key(key_mask(e.key.scancode),down);
            if (down && !e.key.repeat) {
                if (e.key.scancode==SDL_SCANCODE_ESCAPE) running=false;
                if (e.key.scancode==SDL_SCANCODE_F5) reset();
                if (e.key.scancode==SDL_SCANCODE_O) open();
            }
        }
    }
    void pump() {
        const auto pump_start=SDL_GetTicksNS();
        SDL_Event e;while (SDL_PollEvent(&e)) event(e);
        std::optional<std::string> path,problem;
        {std::lock_guard lock(dialog->mutex);if (dialog->ready) {
            dialog->ready=false;path=std::move(dialog->path);problem=std::move(dialog->error);
            dialog->path.clear();dialog->error.clear();
        }}
        if (path) {
            modal=false;input.focus((SDL_GetWindowFlags(window.get())&SDL_WINDOW_INPUT_FOCUS)!=0);
        }
        if (problem && !problem->empty()) error("File selection failed: "+*problem+"\nYou can also drop a .nes file into the window.");
        else if (path && !path->empty()) load(utf8_path(*path));
        std::uint8_t buttons=0;
        if (pad && SDL_GamepadConnected(pad.get())) {
            constexpr SDL_GamepadButton map[]{SDL_GAMEPAD_BUTTON_SOUTH,SDL_GAMEPAD_BUTTON_WEST,
                SDL_GAMEPAD_BUTTON_BACK,SDL_GAMEPAD_BUTTON_START,SDL_GAMEPAD_BUTTON_DPAD_UP,
                SDL_GAMEPAD_BUTTON_DPAD_DOWN,SDL_GAMEPAD_BUTTON_DPAD_LEFT,SDL_GAMEPAD_BUTTON_DPAD_RIGHT};
            for (unsigned i=0;i<8;++i) if (SDL_GetGamepadButton(pad.get(),map[i])) buttons|=1<<i;
        }
        input.gamepad(buttons);
        playback->buttons(input.buttons());
        max_pump_work_us=std::max(max_pump_work_us,(SDL_GetTicksNS()-pump_start)/1000);
    }
    void idle() {
        const auto start=SDL_GetTicksNS();SDL_Delay(1);
        max_sleep_us=std::max(max_sleep_us,(SDL_GetTicksNS()-start)/1000);
    }
    void step() {
        const auto step_start=SDL_GetTicksNS();
        if(last_step) max_loop_gap_us=std::max(max_loop_gap_us,(step_start-last_step)/1000);
        last_step=step_start;
        if (const auto problem=playback->error();!problem.empty()) {
            stop_audio();playback->clear();visible={};redraw=true;
            error("Execution stopped: "+problem+"\nPress O to choose another cartridge.");
            if(smoke)throw std::runtime_error(problem);
        }
        const bool playing=playback->loaded() && !minimized && !modal;
        playback->active(playing);
        redraw=playback->update_frame(visible)||redraw;
        if (!minimized && (redraw || verify_next)) {display->draw(playback->loaded()?&visible:nullptr,smoke && verify_next);redraw=false;}
        if (playing && !running_audio && playback->ready()) {require_sdl(SDL_ResumeAudioStreamDevice(audio.get()));running_audio=true;}
        verify_next=false;
        max_step_work_us=std::max(max_step_work_us,(SDL_GetTicksNS()-step_start)/1000);
    }
    void pause_audio() {playback->active(false);require_sdl(SDL_PauseAudioStreamDevice(audio.get()));running_audio=false;}
    void stop_audio() {pause_audio();require_sdl(SDL_ClearAudioStream(audio.get()));}
    void error(const std::string& message) {
        ++errors;std::cerr<<message<<'\n';
        if (!smoke) {
            input.focus(false);pause_audio();
            (void)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Retro Silicon Emulator",message.c_str(),window.get());
            input.focus((SDL_GetWindowFlags(window.get())&SDL_WINDOW_INPUT_FOCUS)!=0);
        }
    }
    void open() {
        {std::lock_guard lock(dialog->mutex);if (dialog->pending) return;dialog->pending=true;}
        modal=true;input.focus(false);pause_audio();
        static const SDL_DialogFileFilter filters[]{{"NES cartridge","nes"},{"All files","*"}};
        SDL_ShowOpenFileDialog(selected,new std::shared_ptr<DialogResult>(dialog),window.get(),filters,2,nullptr,false);
    }
    void report(std::uint64_t elapsed_ms) const {
        const auto stats=playback->stats();
        std::cout<<"Desktop: renderer="<<display->backend()<<" audio="<<SDL_GetCurrentAudioDriver()
                 <<" frames="<<stats.frames<<" loads="<<loads<<" resets="<<resets<<" errors="<<errors
                 <<" readbacks="<<display->readbacks()<<" peak_audio_bytes="<<stats.peak_audio
                 <<" peak_video_frames="<<stats.peak_video<<" starvation_requests="<<starvation.load()
                 <<" elapsed_ms="<<elapsed_ms<<" simulated_ms="<<stats.ticks*11/236250
                 <<" samples="<<stats.samples<<" max_chunk_work_us="<<stats.max_chunk_us
                 <<" target_audio_bytes="<<stats.target_bytes
                 <<" max_step_work_us="<<max_step_work_us<<" max_loop_gap_us="<<max_loop_gap_us
                 <<" max_pump_work_us="<<max_pump_work_us<<" max_sleep_us="<<max_sleep_us
                 <<" starvation_bytes="<<starvation_bytes.load()<<" first_starvation_tick="<<first_starvation.load()
                 <<" last_starvation_tick="<<last_starvation.load()
                 <<" input_frame_latency_us="<<input_frame_latency_us
                 <<" input_responses="<<input_responses<<" input_min_us="<<input_min_us
                 <<" input_max_us="<<input_max_us<<" input_mean_us="<<(input_responses?input_sum_us/input_responses:0)
                 <<" producer_priority="<<(stats.high_priority?"high":"normal")<<'\n';
    }
    std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window{nullptr,SDL_DestroyWindow};
    std::unique_ptr<Display> display;
    Input input;video::PpuFrame visible;
    std::unique_ptr<SDL_Gamepad,decltype(&SDL_CloseGamepad)> pad{nullptr,SDL_CloseGamepad};
    bool running{true},verify_next{},smoke;
    unsigned loads{},resets{},errors{};
    std::uint64_t input_frame_latency_us{};
    std::uint64_t input_sum_us{},input_min_us{},input_max_us{};
    unsigned input_responses{};
    std::atomic<unsigned> starvation{};
private:
    static void SDLCALL observe(void* userdata,SDL_AudioStream*,int additional,int) {
        if (additional>0) {
            auto& app=*static_cast<Application*>(userdata);
            app.starvation.fetch_add(1,std::memory_order_relaxed);
            app.starvation_bytes.fetch_add(unsigned(additional),std::memory_order_relaxed);
            const auto now=SDL_GetTicks();std::uint64_t empty=0;
            (void)app.first_starvation.compare_exchange_strong(empty,now,std::memory_order_relaxed);
            app.last_starvation.store(now,std::memory_order_relaxed);
        }
    }
    std::atomic<std::uint64_t> starvation_bytes{},first_starvation{},last_starvation{};
    std::shared_ptr<DialogResult> dialog=std::make_shared<DialogResult>();
    std::unique_ptr<SDL_AudioStream,decltype(&SDL_DestroyAudioStream)> audio{nullptr,SDL_DestroyAudioStream};
public:
    // Declared after audio so the producer joins before its stream is destroyed.
    std::unique_ptr<Playback> playback;
private:
    std::uint64_t last_step{},max_step_work_us{},max_loop_gap_us{};
    std::uint64_t max_pump_work_us{},max_sleep_us{};
    bool minimized{},running_audio{},modal{},redraw{true};
};
class Smoke {
public:
    explicit Smoke(std::filesystem::path fixture):path(std::move(fixture)) {
        SDL_VirtualJoystickDesc desc;SDL_INIT_INTERFACE(&desc);
        desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
        desc.naxes=SDL_GAMEPAD_AXIS_COUNT;desc.button_mask=0xffffffff;desc.axis_mask=0x3f;
        desc.name="RSE smoke gamepad";id=SDL_AttachVirtualJoystick(&desc);require_sdl(id!=0);
        joystick.reset(SDL_OpenJoystick(id));require_sdl(bool(joystick));
    }
    ~Smoke() {joystick.reset();if (id) SDL_DetachVirtualJoystick(id);}
    void tick(Application& app,std::uint64_t elapsed) {
        if (input_start && app.visible.samples[32000]==expected_sample) {
            const auto latency=(SDL_GetTicksNS()-input_start)/1000;
            if(!app.input_responses)app.input_frame_latency_us=app.input_min_us=latency;
            app.input_min_us=std::min(app.input_min_us,latency);app.input_max_us=std::max(app.input_max_us,latency);
            app.input_sum_us+=latency;++app.input_responses;input_start=0;
        }
        if (elapsed<500+stage*150) return;
        if (stage<=16) std::cerr<<"Smoke step "<<stage<<'\n';
        auto push=[&](SDL_Event e){require_sdl(SDL_PushEvent(&e));};
        auto key=[&](SDL_Scancode code,bool down) {SDL_Event e{};e.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;e.key.scancode=code;push(e);};
        auto focus=[&](bool gained) {SDL_Event e{};e.type=gained?SDL_EVENT_WINDOW_FOCUS_GAINED:SDL_EVENT_WINDOW_FOCUS_LOST;push(e);};
        auto expect=[](bool ok,const char* reason){if (!ok) throw std::runtime_error(reason);};
        switch(stage++) {
        case 0:focus(true);key(SDL_SCANCODE_X,true);response(0x20);break;
        case 1:expect(app.input.buttons()==1,"keyboard input failed");
            expect(app.playback->memory(0x10)==1,"keyboard did not reach NROM controller reader");focus(false);response(0x21);break;
        case 2:expect(app.input.buttons()==0,"focus did not release keyboard");focus(true);break;
        case 3:expect(app.input.buttons()==0,"keyboard stuck after focus regained");
            app.pad.reset();app.connect(id);expect(bool(app.pad),"virtual standard gamepad not recognized");
            require_sdl(SDL_SetJoystickVirtualButton(joystick.get(),SDL_GAMEPAD_BUTTON_SOUTH,true));response(0x20);break;
        case 4:expect(app.input.buttons()==1,"gamepad mapping failed");
            expect(app.playback->memory(0x10)==1,"gamepad did not reach NROM controller reader");
            require_sdl(SDL_SetJoystickVirtualButton(joystick.get(),SDL_GAMEPAD_BUTTON_SOUTH,false));response(0x21);break;
        case 5:expect(app.input.buttons()==0,"gamepad release failed");key(SDL_SCANCODE_F5,true);break;
        case 6:expect(app.resets==1 && app.playback->tick()<20'000'000,"reset event failed");
            expect(app.visible.number>0 && app.visible.samples[32000]!=0,"smoke did not present an emulated color frame");
            require_sdl(SDL_SetWindowSize(app.window.get(),1000,480));app.verify_next=true;break;
        case 7:app.verify_next=true;require_sdl(SDL_SetWindowSize(app.window.get(),480,720));break;
        case 8:app.verify_next=true;{SDL_Event e{};e.type=SDL_EVENT_DROP_FILE;e.drop.data="rse-nonexistent-smoke-fixture.nes";push(e);}break;
        case 9:expect(app.errors==1 && app.playback->loaded(),"invalid cartridge did not preserve running machine");{
            SDL_Event e{};e.type=SDL_EVENT_DROP_FILE;drop=path.string();e.drop.data=drop.c_str();push(e);}break;
        case 10:expect(app.loads==2,"drop-file replacement failed");app.verify_next=true;
            require_sdl(SDL_MinimizeWindow(app.window.get()));break;
        case 11:require_sdl(SDL_RestoreWindow(app.window.get()));break;
        case 12:app.verify_next=true;break;
        case 13:{SDL_Event e{};e.type=SDL_EVENT_DROP_FILE;drop=path.string()+".unsupported.nes";e.drop.data=drop.c_str();push(e);}break;
        case 14:expect(app.errors==2 && app.playback->loaded(),"unsupported cartridge recovery failed");{
            SDL_Event e{};e.type=SDL_EVENT_DROP_FILE;drop=path.string()+".invalid.nes";e.drop.data=drop.c_str();push(e);}break;
        case 15:expect(app.errors==3 && app.playback->loaded(),"malformed cartridge recovery failed");break;
        case 16:{const auto before=app.playback->stats().samples;SDL_Delay(150);
            expect(app.playback->stats().samples>before+4800,"audio producer stopped during a blocked GUI");break;}
        default:break;
        }
    }
    void finish(Application& app) {
        if (stage<16 || app.display->readbacks()<3 || app.starvation.load() || app.input_responses!=4)
            throw std::runtime_error("desktop smoke acceptance failed: stage="+std::to_string(stage)+
                " readbacks="+std::to_string(app.display->readbacks())+" starvation="+std::to_string(app.starvation.load()));
        SDL_Event e{};e.type=SDL_EVENT_WINDOW_CLOSE_REQUESTED;require_sdl(SDL_PushEvent(&e));app.pump();
        if (app.running) throw std::runtime_error("window close failed");
    }
private:
    void response(std::uint16_t sample) {expected_sample=sample;input_start=SDL_GetTicksNS();}
    std::filesystem::path path;std::string drop;unsigned stage{};SDL_JoystickID id{};
    std::uint64_t input_start{};
    std::uint16_t expected_sample{};
    std::unique_ptr<SDL_Joystick,decltype(&SDL_CloseJoystick)> joystick{nullptr,SDL_CloseJoystick};
};
}
} // namespace rse::desktop
int run_desktop(const std::filesystem::path& cartridge,unsigned seconds,bool smoke) {
    using namespace rse::desktop;
    try {
        Application app(smoke);if (smoke) app.step();
        if (!cartridge.empty()) app.load(cartridge);
        if (smoke && !app.playback->loaded()) throw std::runtime_error("smoke fixture could not be loaded");
        std::unique_ptr<Smoke> script;if (smoke) script=std::make_unique<Smoke>(cartridge);
        const auto start=SDL_GetTicks();
        while (app.running && (!seconds || SDL_GetTicks()-start<std::uint64_t(seconds)*1000)) {
            app.pump();if(!app.running)break;
            if (script) script->tick(app,SDL_GetTicks()-start);app.step();app.idle();
        }
        if (script) std::cerr<<"Smoke stopping audio\n";
        app.stop_audio();const auto elapsed=SDL_GetTicks()-start;app.report(elapsed);
        if (script) {
            if(elapsed<std::uint64_t(seconds)*1000) throw std::runtime_error("smoke exited before its requested duration");
            std::cerr<<"Smoke closing window\n";script->finish(app);
            app.pad.reset();script.reset();std::cerr<<"Smoke device teardown complete\n";
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
