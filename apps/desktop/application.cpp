// SPDX-License-Identifier: Apache-2.0
#include "application.hpp"
#include "display.hpp"
#include "playback.hpp"
#include "playback_timing.hpp"
#include "smoke.hpp"
#include "trainer.hpp"
#include <rse/session/builtins.hpp>
#include <rse/session/time.hpp>
#include "nes_controls.hpp"
#include <atomic>
#include <future>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#ifdef RSE_POLICY_ENABLED
#include <rse/session/policy_playback.hpp>
#endif

namespace rse::desktop {
namespace {
enum class DialogKind { rom, profile, checkpoint, model, bios };
struct DialogResult {
    std::mutex mutex;
    bool pending{},ready{};
    std::string path,error;
    DialogKind kind{DialogKind::rom};
};
std::string display_name(const std::filesystem::path& path) {
    if(path.empty()) return "None";
    const auto name=path.filename().u8string();
    // SDL debug text uses ASCII glyphs. Preserve the original path internally.
    std::string result;
    for(const auto ch:name) result+=ch>=32 && ch<127 ? char(ch) : '?';
    return result;
}
void SDLCALL selected(void* userdata,const char* const* files,int) {
    std::unique_ptr<std::shared_ptr<DialogResult>> token(static_cast<std::shared_ptr<DialogResult>*>(userdata));
    auto& result=**token;std::lock_guard lock(result.mutex);
    if (!files) result.error=SDL_GetError();else if (files[0]) result.path=files[0];
    result.pending=false;result.ready=true;
}
std::uint16_t key_mask(SDL_Scancode key,bool gba) {
    switch (key) {
    case SDL_SCANCODE_X:return 1;case SDL_SCANCODE_Z:return 2;
    case SDL_SCANCODE_RSHIFT:return 4;case SDL_SCANCODE_RETURN:return 8;
    case SDL_SCANCODE_UP:return 16;case SDL_SCANCODE_DOWN:return 32;
    case SDL_SCANCODE_LEFT:return 64;case SDL_SCANCODE_RIGHT:return 128;
    case SDL_SCANCODE_Q:return gba ? 0x100 : 0;
    case SDL_SCANCODE_W:return gba ? 0x200 : 0;
    default:return 0;
    }
}
class Application final : public SmokeHost {
public:
    explicit Application(bool test,std::string_view system,DesktopLoad loader,DesktopWorkspace workspace,
                         const std::filesystem::path& bios)
        : smoke(test),system_id(system),loader(std::move(loader)),workspace_(std::move(workspace)),bios_(bios) {
        if(workspace_.trainer) trainer_=std::make_unique<Trainer>(*workspace_.trainer);
        window.reset(SDL_CreateWindow("Retro Silicon Emulator",960,720,SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY));
        require_sdl(bool(window));display=std::make_unique<Display>(window.get());
        playback=std::make_unique<Playback>(nullptr);
        int count{};auto* ids=SDL_GetGamepads(&count);
        for (int i=0;i<count && !pad;++i) connect(ids[i]);SDL_free(ids);
    }
    void load(const std::filesystem::path& path) {
        if (system_id=="gba" && bios_.empty()) {
            pending_media_=path;
            open(DialogKind::bios);
            return;
        }
        (void)load_media(path,bios_);
    }
    bool load_media(const std::filesystem::path& path,const std::filesystem::path& bios) {
        try {
            if (system_id=="gba") flush_save();
            auto next=loader ? loader(path) : session::load(system_id,path,bios);
            replace_session(std::move(next),path);
            model_name_.clear();
            return true;
        } catch (const std::exception& e) {
            const auto help=system_id=="gba" ? "\nSelect a GBA ROM with O and a 16 KiB BIOS with F12." :
                "\nChoose a supported mapper-0 iNES image.";
            error(std::string("Cannot open cartridge: ")+e.what()+help);
            return false;
        }
    }
    void replace_session(std::unique_ptr<session::Session> next,const std::filesystem::path& path) {
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
            input.clear();visible={};running_audio=false;paused=false;
            rom_=path;
            (void)SDL_SetWindowTitle(window.get(),title.c_str());redraw=true;++loads;
    }
    void start_trainer(bool resume) {
        if(!trainer_) throw std::logic_error("Launch with trainer settings to enable training");
        const auto checkpoint=checkpoint_.empty() ? trainer_->checkpoint() : checkpoint_;
        if(resume && checkpoint.empty()) throw std::logic_error("Select a checkpoint with F4 first");
        trainer_->start(rom_,workspace_.profile,workspace_.observation,resume ? checkpoint : std::filesystem::path{});
        if(resume) checkpoint_.clear();
        workspace_status_.clear();
        workspace_.visible=true;
        redraw=true;
    }
    void load_model(const std::filesystem::path& path) {
#ifdef RSE_POLICY_ENABLED
        if(rom_.empty() || loader || model_future_.valid())
            throw std::logic_error("Select a ROM and wait for any model load to finish");
        pending_rom_=rom_;
        pending_model_=path;
        model_future_=std::async(std::launch::async,[rom=rom_,path] {
            const auto bytes=policy::read_file(rom,64*1024);
            auto model=std::make_shared<policy::Model>(path,policy::Provider::automatic);
            return std::make_unique<policy::Playback>(std::move(model),bytes);
        });
        workspace_status_="Loading model";
#else
        (void)path;
        throw std::logic_error("Model loading requires RSE_BUILD_POLICY");
#endif
    }
    void poll_workspace() {
        if(trainer_ && trainer_->poll() && workspace_.visible) redraw=true;
#ifdef RSE_POLICY_ENABLED
        if(model_future_.valid() && model_future_.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            try {
                auto model=model_future_.get();
                if(rom_!=pending_rom_) throw std::logic_error("ROM changed during model load");
                replace_session(std::move(model),pending_rom_);
                model_name_=display_name(pending_model_);
                workspace_status_.clear();
            } catch(const std::exception& e) {workspace_status_=e.what();std::cerr<<workspace_status_<<'\n';}
            redraw=true;
        }
#endif
    }
    void workspace_key(SDL_Scancode code) {
        if(code==SDL_SCANCODE_F1) {workspace_.visible=!workspace_.visible;redraw=true;return;}
        if(!workspace_.visible) return;
        try {
            switch(code) {
            case SDL_SCANCODE_F2: open(DialogKind::profile);break;
            case SDL_SCANCODE_F3: open(DialogKind::model);break;
            case SDL_SCANCODE_F4: open(DialogKind::checkpoint);break;
            case SDL_SCANCODE_F6: start_trainer(false);break;
            case SDL_SCANCODE_F7:
                if(workspace_.observation.empty()) workspace_.observation="memory";
                else if(workspace_.observation=="memory") workspace_.observation="screen";
                else if(workspace_.observation=="screen") workspace_.observation="hybrid";
                else workspace_.observation.clear();
                break;
            case SDL_SCANCODE_F8:
                if(trainer_) {trainer_->stop();workspace_status_.clear();}
                break;
            case SDL_SCANCODE_F9: start_trainer(true);break;
            case SDL_SCANCODE_F10: if(!model_name_.empty()) {auto path=rom_;load(path);}break;
            case SDL_SCANCODE_F11: watch_=watch_==0x13 ? 0x10 : watch_+1;break;
            default: return;
            }
        } catch(const std::exception& e) {workspace_status_=e.what();}
        redraw=true;
    }
    WorkspaceOverlay overlay() const {
        const auto number=[](std::optional<double> value) {
            if(!value) return std::string("--");
            std::ostringstream out;out<<std::fixed<<std::setprecision(2)<<*value;return out.str();
        };
        std::ostringstream memory;
        memory<<"F11 RAM $"<<std::hex<<std::uppercase<<watch_<<" = ";
        if(const auto value=playback->peek("cpu.ram",watch_)) memory<<'$'<<*value;
        else memory<<"--";
        auto buttons=input.buttons();
        std::string control="Manual";
#ifdef RSE_POLICY_ENABLED
        (void)playback->inspect([&](const session::Session& machine) {
            if(const auto* player=dynamic_cast<const policy::Playback*>(&machine)) {
                buttons=player->active_buttons();
                if(!player->manual()) control="Policy";
            }
            return true;
        });
#endif
        memory<<"   "<<control<<" $"<<unsigned(buttons);
        WorkspaceOverlay result{{"AI workspace  F1 hide",
            "O  ROM: "+display_name(rom_),"F2 Task: "+display_name(workspace_.profile),
            "F7 Observation: "+(workspace_.observation.empty()?"profile":workspace_.observation),
            "F3 Model: "+(model_name_.empty()?(loader?"startup policy":"None"):model_name_),
            "F4 Checkpoint: "+display_name(checkpoint_.empty() && trainer_ ? trainer_->checkpoint() : checkpoint_),
            "F6 Start  F8 Stop  F9 Resume  F10 Unload",memory.str()},nullptr};
        if(!workspace_status_.empty()) result.lines.push_back(workspace_status_);
        if(trainer_) {
            const auto& data=trainer_->metrics();
            result.lines.push_back(trainer_->status());
            result.lines.push_back("Steps "+std::to_string(data.steps)+"   /s "+number(data.rate));
            result.lines.push_back("Reward "+number(data.reward)+"   Loss "+number(data.loss));
            result.lines.push_back("Trainer action "+(data.action ? std::to_string(*data.action) : "--"));
            result.lines.push_back("Trainer preview");
            result.preview=&trainer_->preview();
        } else result.lines.push_back("Trainer is not configured");
        return result;
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
    void close() {
        try { flush_save();running=false; }
        catch (const std::exception& e) { paused=true;error(e.what()); }
    }
    void event(const SDL_Event& e) {
        if (e.type==SDL_EVENT_QUIT || e.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED) {close();return;}
        if (e.type==SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || e.type==SDL_EVENT_WINDOW_EXPOSED) redraw=true;
        if (e.type==SDL_EVENT_WINDOW_FOCUS_LOST) {input.focus(false);return;}
        if (e.type==SDL_EVENT_WINDOW_FOCUS_GAINED) {input.focus(true);return;}
        if (e.type==SDL_EVENT_WINDOW_MINIMIZED) {minimized=true;pause_audio();return;}
        if (e.type==SDL_EVENT_WINDOW_RESTORED) {minimized=false;redraw=true;return;}
        if (e.type==SDL_EVENT_DROP_FILE && e.drop.data) {load(utf8_path(e.drop.data));return;}
        if (e.type==SDL_EVENT_GAMEPAD_ADDED) {connect(e.gdevice.which);return;}
        if (e.type==SDL_EVENT_GAMEPAD_REMOVED && pad && SDL_GetGamepadID(pad.get())==e.gdevice.which) {pad.reset();input.gamepad(0);return;}
        if (e.type==SDL_EVENT_KEY_DOWN || e.type==SDL_EVENT_KEY_UP) {
            const bool down=e.type==SDL_EVENT_KEY_DOWN;input.key(key_mask(e.key.scancode,system_id=="gba"),down);
            if (down && !e.key.repeat) {
                if (e.key.scancode==SDL_SCANCODE_ESCAPE) {close();return;}
                if (e.key.scancode==SDL_SCANCODE_F5) reset();
                if (e.key.scancode==SDL_SCANCODE_O) open();
                if (e.key.scancode==SDL_SCANCODE_F12 && system_id=="gba") open(DialogKind::bios);
                if (e.key.scancode==SDL_SCANCODE_P && playback->loaded()) {
                    paused=!paused;
                    if (paused) { input.clear();pause_audio(); }
                }
                workspace_key(e.key.scancode);
            }
        }
    }
    void pump() override {
        const auto pump_start=SDL_GetTicksNS();
        SDL_Event e;while (SDL_PollEvent(&e)) event(e);
        std::optional<std::string> path,problem;
        DialogKind kind=DialogKind::rom;
        {std::lock_guard lock(dialog->mutex);if (dialog->ready) {
            dialog->ready=false;path=std::move(dialog->path);problem=std::move(dialog->error);
            kind=dialog->kind;
            dialog->path.clear();dialog->error.clear();
        }}
        if (path) {
            modal=false;input.focus((SDL_GetWindowFlags(window.get())&SDL_WINDOW_INPUT_FOCUS)!=0);
        }
        if (problem && !problem->empty()) error("File selection failed: "+*problem);
        else if (path && !path->empty()) {
            try {
                auto selection=utf8_path(*path);
                switch(kind) {
                case DialogKind::rom: load(selection);break;
                case DialogKind::model: load_model(selection);break;
                case DialogKind::bios:
                    if (pending_media_.empty() || load_media(pending_media_,selection)) bios_=selection;
                    break;
                case DialogKind::profile: workspace_.profile=std::move(selection);workspace_status_.clear();break;
                case DialogKind::checkpoint: checkpoint_=std::move(selection);workspace_status_.clear();break;
                }
                redraw=true;
            } catch(const std::exception& e) {workspace_status_=e.what();redraw=true;}
        }
        if (path && kind==DialogKind::bios) pending_media_.clear();
        poll_workspace();
        std::uint16_t buttons=0;
        if (pad && SDL_GamepadConnected(pad.get())) {
            constexpr SDL_GamepadButton map[]{SDL_GAMEPAD_BUTTON_SOUTH,SDL_GAMEPAD_BUTTON_WEST,
                SDL_GAMEPAD_BUTTON_BACK,SDL_GAMEPAD_BUTTON_START,SDL_GAMEPAD_BUTTON_DPAD_UP,
                SDL_GAMEPAD_BUTTON_DPAD_DOWN,SDL_GAMEPAD_BUTTON_DPAD_LEFT,SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
                SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER};
            const unsigned count=system_id=="gba" ? std::size(map) : 8;
            for (unsigned i=0;i<count;++i) if (SDL_GetGamepadButton(pad.get(),map[i])) buttons|=1<<i;
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
        const bool playing=playback->loaded() && !minimized && !modal && !paused;
        playback->active(playing);
        redraw=playback->update_frame(visible)||redraw;
        if (!minimized && (redraw || verify_next)) {
            const auto workspace=workspace_.visible ? std::optional{overlay()} : std::nullopt;
            display->draw(playback->loaded()?&visible:nullptr,smoke && verify_next,
                workspace ? &*workspace : nullptr,system_id=="gba");
            redraw=false;
        }
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
    void flush_save() {
        stop_audio();
        playback->flush_save();
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
    void open(DialogKind kind=DialogKind::rom) {
        {std::lock_guard lock(dialog->mutex);if (dialog->pending || dialog->ready) return;
            dialog->pending=true;dialog->kind=kind;}
        modal=true;input.focus(false);pause_audio();
        static const SDL_DialogFileFilter filters[]{
            {"NES cartridge","nes"},{"Task profile","json"},{"PPO checkpoint","zip"},{"ONNX policy bundle","rsep"},
            {"GBA BIOS","bin;rom"}};
        static const SDL_DialogFileFilter gba_rom{"GBA cartridge","gba"};
        const auto* filter=kind==DialogKind::rom && system_id=="gba" ?
            &gba_rom : &filters[static_cast<unsigned>(kind)];
        SDL_ShowOpenFileDialog(selected,new std::shared_ptr<DialogResult>(dialog),window.get(),
            filter,1,nullptr,false);
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
        if(trainer_) std::cout<<" trainer_steps="<<trainer_->metrics().steps
            <<" trainer_preview="<<trainer_->preview().number;
        std::cout<<" producer_priority="<<(stats.high_priority?"high":"normal")<<'\n';
    }
    [[nodiscard]] bool is_running() const noexcept {return running;}
    [[nodiscard]] bool has_machine() const {return playback->loaded();}
    [[nodiscard]] SmokeObservation observation() const override {
        return {running,playback->loaded(),input.buttons(),playback->tick(),visible.number,
                playback->stats().samples,loads,resets,errors,display->readbacks(),starvation.load(),visible.pixels,
                visible.format,running_audio};
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
    DesktopWorkspace workspace_;
    std::filesystem::path rom_,checkpoint_,bios_,pending_media_;
    std::unique_ptr<Trainer> trainer_;
    std::string model_name_,workspace_status_;
    std::uint32_t watch_{0x10};
#ifdef RSE_POLICY_ENABLED
    std::filesystem::path pending_rom_,pending_model_;
    std::future<std::unique_ptr<policy::Playback>> model_future_;
#endif
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
    bool minimized{},running_audio{},modal{},paused{},redraw{true};
};

}
} // namespace rse::desktop
int run_desktop(const std::filesystem::path& cartridge,unsigned seconds,bool smoke,std::string_view system,
                DesktopLoad loader,DesktopFinish finish,DesktopWorkspace workspace,const std::filesystem::path& bios) {
    using namespace rse::desktop;
    try {
        const bool autostart=workspace.autostart;
        Application app(smoke,system,std::move(loader),std::move(workspace),bios);if (smoke) app.step();
        if (!cartridge.empty()) app.load(cartridge);
        if ((smoke || finish) && !app.has_machine()) throw std::runtime_error("Requested session could not be loaded");
        if(autostart) app.start_trainer(false);
        std::unique_ptr<Smoke> script;if (smoke) script=std::make_unique<Smoke>(cartridge,system=="gba");
        const auto start=SDL_GetTicks();
        while (app.is_running() && (!seconds || SDL_GetTicks()-start<std::uint64_t(seconds)*1000)) {
            app.pump();if(!app.is_running())break;
            if (script) script->tick(app,SDL_GetTicks()-start);app.step();app.idle();
        }
        if (script) std::cerr<<"Smoke stopping audio\n";
        app.flush_save();const auto elapsed=SDL_GetTicks()-start;app.report(elapsed,script?script->metrics():SmokeMetrics{});
        if(finish) app.finish(finish);
        if (script) {
            if(elapsed<std::uint64_t(seconds)*1000) throw std::runtime_error("smoke exited before its requested duration");
            std::cerr<<"Smoke closing window\n";script->finish(app);
            app.select_gamepad(0);script.reset();std::cerr<<"Smoke device teardown complete\n";
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
