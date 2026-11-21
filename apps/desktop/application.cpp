// SPDX-License-Identifier: Apache-2.0
#include "application.hpp"
#include "display.hpp"
#include "playback.hpp"
#include "playback_timing.hpp"
#include "smoke.hpp"
#include <rse/session/builtins.hpp>
#include <rse/session/time.hpp>
#include "nes_controls.hpp"
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
class Application final : public SmokeHost {
public:
    explicit Application(bool test,std::string_view system,DesktopLoad loader)
        : smoke(test),system_id(system),loader(std::move(loader)) {
        window.reset(SDL_CreateWindow("Retro Silicon Emulator",960,720,SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY));
        require_sdl(bool(window));display=std::make_unique<Display>(window.get());
        playback=std::make_unique<Playback>(nullptr);
        int count{};auto* ids=SDL_GetGamepads(&count);
        for (int i=0;i<count && !pad;++i) connect(ids[i]);SDL_free(ids);
    }
    void load(const std::filesystem::path& path) {
        try {
            auto next=loader ? loader(path) : session::load(system_id,path);
            const auto filename=path.filename().u8string();
            const auto title=std::string("Retro Silicon Emulator - ")+std::string(reinterpret_cast<const char*>(filename.data()),filename.size());
            if(next->info().audio && !audio) {
                require_sdl(SDL_InitSubSystem(SDL_INIT_AUDIO));
                const auto format=*next->info().audio;
                SDL_AudioSpec spec{SDL_AUDIO_S16,int(format.channels),int(format.sample_rate)};
                std::unique_ptr<SDL_AudioStream,decltype(&SDL_DestroyAudioStream)> stream(
                    SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,observe,this),SDL_DestroyAudioStream);
                if(!stream)throw std::runtime_error(std::string("Audio device unavailable: ")+SDL_GetError());
                auto producer=std::make_unique<Playback>(stream.get());
                producer->replace(std::move(next));
                stop_audio();playback=std::move(producer);audio=std::move(stream);
            } else {stop_audio();playback->replace(std::move(next));}
            input.clear();visible={};running_audio=false;
            (void)SDL_SetWindowTitle(window.get(),title.c_str());redraw=true;++loads;
        } catch (const std::exception& e) {error(std::string("Cannot open cartridge: ")+e.what()+"\nChoose a supported mapper-0 iNES image. The previous cartridge is retained.");}
    }
    void finish(const DesktopFinish& callback) const {
        if(!playback->error().empty()) throw std::runtime_error(playback->error());
        if(!playback->inspect([&](const session::Session& machine){callback(machine);return true;}))
            throw std::runtime_error("No policy session available to record");
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
    void pump() override {
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
        playback->input(default_controls(system_id,input.buttons()));
        max_pump_work_us=std::max(max_pump_work_us,(SDL_GetTicksNS()-pump_start)/1000);
    }
    void idle() {
        const auto start=SDL_GetTicksNS();SDL_Delay(timing::ui_idle_ms);
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
        if (playing && playback->has_audio() && !running_audio && playback->ready()) {require_sdl(SDL_ResumeAudioStreamDevice(audio.get()));running_audio=true;}
        verify_next=false;
        max_step_work_us=std::max(max_step_work_us,(SDL_GetTicksNS()-step_start)/1000);
    }
    void pause_audio() {
        // Serialize with the producer before touching its SDL stream. Retain
        // queued PCM for modal/minimize pauses; resume waits for priming.
        playback->active(false);
        if(audio)require_sdl(SDL_PauseAudioStreamDevice(audio.get()));
        running_audio=false;
    }
    void stop_audio() {
        pause_audio();
        // Load/reset/fault recovery additionally discard the old timeline.
        if(audio)require_sdl(SDL_ClearAudioStream(audio.get()));
    }
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
    void report(std::uint64_t elapsed_ms,const SmokeMetrics& metrics) const {
        const auto stats=playback->stats();
        std::cout<<"Desktop: renderer="<<display->backend()<<" audio="<<SDL_GetCurrentAudioDriver()
                 <<" frames="<<stats.frames<<" loads="<<loads<<" resets="<<resets<<" errors="<<errors
                 <<" readbacks="<<display->readbacks()<<" peak_audio_bytes="<<stats.peak_audio
                 <<" peak_video_frames="<<stats.peak_video<<" starvation_requests="<<starvation.load()
                 <<" elapsed_ms="<<elapsed_ms<<" simulated_ms="<<stats.simulated_ms
                 <<" samples="<<stats.samples<<" max_chunk_work_us="<<stats.max_chunk_us
                 <<" target_audio_bytes="<<stats.target_bytes
                 <<" max_step_work_us="<<max_step_work_us<<" max_loop_gap_us="<<max_loop_gap_us
                 <<" max_pump_work_us="<<max_pump_work_us<<" max_sleep_us="<<max_sleep_us
                 <<" starvation_bytes="<<starvation_bytes.load()<<" first_starvation_tick="<<first_starvation.load()
                 <<" last_starvation_tick="<<last_starvation.load();
        metrics.report(std::cout);
        std::cout<<" producer_priority="<<(stats.high_priority?"high":"normal")<<'\n';
    }
    [[nodiscard]] bool is_running() const noexcept {return running;}
    [[nodiscard]] bool has_machine() const {return playback->loaded();}
    [[nodiscard]] SmokeObservation observation() const override {
        return {running,playback->loaded(),input.buttons(),playback->tick(),visible.number,
                playback->stats().samples,loads,resets,errors,display->readbacks(),starvation.load(),visible.pixels};
    }
    [[nodiscard]] std::optional<std::uint8_t> inspect_memory(std::uint16_t address) const override {
        std::optional<std::uint8_t> value;
        (void)playback->inspect([&](const session::Session& machine){value=smoke_memory(machine,address);return true;});
        return value;
    }
    void select_gamepad(SDL_JoystickID id) override {
        pad.reset();
        if(id) {
            connect(id);
            if(!pad)throw std::runtime_error("virtual standard gamepad not recognized");
        }
    }
    void request_readback() override {verify_next=true;}
    void resize_window(int width,int height) override {require_sdl(SDL_SetWindowSize(window.get(),width,height));}
    void minimize_window(bool value) override {
        require_sdl(value?SDL_MinimizeWindow(window.get()):SDL_RestoreWindow(window.get()));
    }
private:
    std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window{nullptr,SDL_DestroyWindow};
    std::unique_ptr<Display> display;
    Input input;Frame visible;
    std::unique_ptr<SDL_Gamepad,decltype(&SDL_CloseGamepad)> pad{nullptr,SDL_CloseGamepad};
    bool running{true},verify_next{},smoke;
    std::string system_id;
    DesktopLoad loader;
    unsigned loads{},resets{},errors{};
    std::atomic<unsigned> starvation{};
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
    // Declared after audio so the producer joins before its stream is destroyed.
    std::unique_ptr<Playback> playback;
    std::uint64_t last_step{},max_step_work_us{},max_loop_gap_us{};
    std::uint64_t max_pump_work_us{},max_sleep_us{};
    bool minimized{},running_audio{},modal{},redraw{true};
};

}
} // namespace rse::desktop
int run_desktop(const std::filesystem::path& cartridge,unsigned seconds,bool smoke,std::string_view system,
                DesktopLoad loader,DesktopFinish finish) {
    using namespace rse::desktop;
    try {
        Application app(smoke,system,std::move(loader));if (smoke) app.step();
        if (!cartridge.empty()) app.load(cartridge);
        if ((smoke || finish) && !app.has_machine()) throw std::runtime_error("Requested session could not be loaded");
        std::unique_ptr<Smoke> script;if (smoke) script=std::make_unique<Smoke>(cartridge);
        const auto start=SDL_GetTicks();
        while (app.is_running() && (!seconds || SDL_GetTicks()-start<std::uint64_t(seconds)*1000)) {
            app.pump();if(!app.is_running())break;
            if (script) script->tick(app,SDL_GetTicks()-start);app.step();app.idle();
        }
        if (script) std::cerr<<"Smoke stopping audio\n";
        app.stop_audio();const auto elapsed=SDL_GetTicks()-start;app.report(elapsed,script?script->metrics():SmokeMetrics{});
        if(finish) app.finish(finish);
        if (script) {
            if(elapsed<std::uint64_t(seconds)*1000) throw std::runtime_error("smoke exited before its requested duration");
            std::cerr<<"Smoke closing window\n";script->finish(app);
            app.select_gamepad(0);script.reset();std::cerr<<"Smoke device teardown complete\n";
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
