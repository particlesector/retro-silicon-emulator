// SPDX-License-Identifier: Apache-2.0
#include "playback.hpp"
#include "acceptance_programs.hpp"
#include "nes_controls.hpp"
#include "fake_machine_session.hpp"
#include <rse/systems/nes/session.hpp>
#include <rse/systems/nes/palette.hpp>
#include <atomic>
#include <iostream>

namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class Predicate> void wait(Predicate done) {
    const auto start=SDL_GetTicks();
    while(!done()){check(SDL_GetTicks()-start<3000,"producer response timeout");SDL_Delay(1);}
}
void SDLCALL observe(void* data,SDL_AudioStream*,int additional,int) {
    if(additional>0)++*static_cast<std::atomic<unsigned>*>(data);
}
rse::nes::MachineState nes_state(const rse::desktop::Playback& producer) {
    rse::nes::MachineState result;
    (void)producer.inspect([&](const rse::session::Session& machine){result=dynamic_cast<const rse::nes::Session&>(machine).diagnostics().state();return true;});return result;
}
std::optional<std::uint8_t> memory(const rse::desktop::Playback& producer,unsigned address) {
    std::optional<std::uint8_t> result;
    (void)producer.inspect([&](const rse::session::Session& machine){result=dynamic_cast<const rse::nes::Session&>(machine).diagnostics().inspect_memory(address);return true;});return result;
}
void run() {
    {
        rse::desktop::Playback silent(nullptr);
        silent.replace(std::make_unique<rse::testing::FakeMachineSession>(true,false));
        silent.active(true);wait([&]{return silent.tick()>=2000;});silent.active(false);
        check(silent.error().empty(),"silent playback required an audio device");
        bool rejected=false;
        try{silent.replace(std::make_unique<rse::testing::FakeMachineSession>());}
        catch(const rse::session::UnsupportedOperation&){rejected=true;}
        check(rejected && silent.loaded(),"unsupported audio replacement discarded silent session");
    }
    std::atomic<unsigned> starvation{};
    SDL_AudioSpec spec{SDL_AUDIO_S16,1,48000};
    std::unique_ptr<SDL_AudioStream,decltype(&SDL_DestroyAudioStream)> audio(
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,observe,&starvation),SDL_DestroyAudioStream);
    check(bool(audio),"dummy audio device");
    const auto cartridge=rse::acceptance::av_input_program().cartridge();
    rse::desktop::Playback producer(audio.get());
    producer.replace(std::make_unique<rse::nes::Session>(cartridge,rse::nes::Profile{.capture_audio=true}));
    producer.input(rse::desktop::nes_controls(0xff));producer.active(true);wait([&]{return producer.ready();});producer.active(false);
    const auto primed=nes_state(producer);
    check(primed.buttons==0xff,"initial all-buttons input was not scheduled");
    rse::nes::Machine reference(cartridge,rse::nes::Profile{.capture_audio=true});
    reference.schedule_input(1,0xff);reference.advance_to(primed.tick);
    check(primed.cpu==reference.state().cpu && primed.cpu_cycles==reference.state().cpu_cycles,"producer changed CPU execution");
    for(unsigned address=0;address<2048;++address)
        check(memory(producer,address)==reference.inspect_memory(address),"producer changed RAM");
    check(producer.stats().samples==reference.take_audio().size(),"producer PCM clock mismatch");
    rse::desktop::Frame visible;
    check(primed.ppu.frames>=2 && producer.update_frame(visible),"latest-frame handoff missing");
    check(visible.number==reference.frame().number && visible.pixels.size()==reference.frame().samples.size() && std::equal(visible.pixels.begin(),visible.pixels.end(),reference.frame().samples.begin(),[](auto color,auto sample){return color==rse::nes::display_color(sample);}),"handoff returned a stale frame");
    check(!producer.update_frame(visible),"unchanged frame reported as new");
    SDL_Delay(40);check(producer.tick()==primed.tick,"paused producer advanced");
#ifdef NDEBUG
    // Model a blocked GUI without servicing producer commands. Audio must keep
    // running independently; no emulation or audio code runs on this thread.
    producer.active(true);wait([&]{return producer.ready();});
    check(SDL_ResumeAudioStreamDevice(audio.get()),"resume dummy device");
    const auto before=producer.stats().samples;SDL_Delay(200);
    check(producer.stats().samples>before+4800,"producer stopped while GUI was blocked");
    check(starvation.load()==0,"producer starved during blocked GUI");
#endif
    producer.active(false);check(SDL_PauseAudioStreamDevice(audio.get()),"pause dummy device");
    check(SDL_ClearAudioStream(audio.get()),"clear dummy device");producer.reset();
    check(producer.tick()==0 && memory(producer,0)==0 && nes_state(producer).buttons==0,"producer reset did not reconstruct machine");
    check(producer.update_frame(visible) && visible.number==0,"reset retained a published frame");
    for(bool replace:{false,true}) {
        check(SDL_ClearAudioStream(audio.get()),"clear before input replay");
        if(replace) {
            producer.replace(std::make_unique<rse::nes::Session>(cartridge,rse::nes::Profile{.capture_audio=true}));
            check(producer.update_frame(visible) && visible.number==0,"replacement retained a published frame");
        }
        producer.input(rse::desktop::nes_controls(0xff));producer.active(true);wait([&]{return producer.ready();});producer.active(false);
        check(nes_state(producer).buttons==0xff,replace?"replacement lost all-buttons input":"reset lost all-buttons input");
        check(producer.update_frame(visible) && visible.number==nes_state(producer).ppu.frames,"input replay did not publish the latest frame");
    }
    check(SDL_ClearAudioStream(audio.get()),"clear before faulted replacement");
    NesProgram bad;bad.emit({0x02});producer.replace(std::make_unique<rse::nes::Session>(bad.cartridge(),rse::nes::Profile{.capture_audio=true}));
    producer.active(true);wait([&]{return !producer.error().empty();});producer.active(false);
    check(SDL_ClearAudioStream(audio.get()),"clear faulted stream");
    producer.replace(std::make_unique<rse::nes::Session>(cartridge,rse::nes::Profile{.capture_audio=true}));
    producer.active(true);wait([&]{return producer.ready();});
    check(producer.error().empty() && producer.tick()>0,"producer did not recover after failed execution");
    producer.active(false);SDL_ClearAudioStream(audio.get());
    producer.replace(std::make_unique<rse::testing::FakeMachineSession>());
    producer.input({{"panel",{"fire"}}});producer.active(true);wait([&]{return producer.ready();});producer.active(false);
    check(producer.inspect([](const rse::session::Session& machine){return dynamic_cast<const rse::testing::FakeMachineSession&>(machine).pressed();}),"generic named input");
    visible={};check(producer.update_frame(visible) && visible.format.width==3 && visible.format.height==2 && visible.pixels.size()==6 && visible.pixels[0].r==255,"generic frame conversion");
    const auto owned=visible.pixels;
    producer.input({});producer.active(true);wait([&]{return producer.ready();});producer.active(false);
    // Primed audio may need no refill; drain it before checking the new input.
    SDL_ClearAudioStream(audio.get());producer.active(true);wait([&]{return producer.ready();});producer.active(false);
    check(producer.inspect([](const rse::session::Session& machine){return !dynamic_cast<const rse::testing::FakeMachineSession&>(machine).pressed();}),"omitted port was not released");
    producer.reset();check(owned[0].r==255 && producer.tick()==0,"generic owning frame/reset");
    for(bool video:{false,true}) {
        SDL_ClearAudioStream(audio.get());producer.replace(std::make_unique<rse::testing::FakeMachineSession>(video,false));
        producer.active(true);wait([&]{return producer.tick()>2000;});producer.active(false);
        check(!producer.has_audio() && producer.error().empty(),"absent audio stalled producer");
        // Frequent GUI notifications must not run a silent system unpaced.
        const auto start_tick=producer.tick(),start_ms=SDL_GetTicks();
        producer.active(true);
        do {producer.active(true);SDL_Delay(1);}while(SDL_GetTicks()-start_ms<20);
        producer.active(false);
        check(producer.tick()-start_tick<40000,"GUI wakes accelerated silent playback");
        visible={};check(producer.update_frame(visible)==video,"optional video publication");
    }
    SDL_ClearAudioStream(audio.get());producer.replace(std::make_unique<rse::testing::FakeMachineSession>());
    bool invalid=false;try{producer.input({{"invalid",{}}});}catch(const std::invalid_argument&){invalid=true;}
    check(invalid && producer.error().empty() && producer.tick()==0,"input validation was not atomic");
    producer.replace(std::make_unique<rse::testing::FakeMachineSession>(true,true,true));
    producer.active(true);wait([&]{return !producer.error().empty();});producer.active(false);
    SDL_ClearAudioStream(audio.get());producer.replace(std::make_unique<rse::testing::FakeMachineSession>());
    producer.active(true);wait([&]{return producer.ready();});producer.active(false);
    check(producer.error().empty(),"generic replacement did not recover");
}
}
int main() {
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER,"dummy");
    // Prime several frames while paused, so latest-frame selection is checked
    // deterministically without relying on real-time consumption or GPU work.
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES,"2048");
    if(!SDL_Init(SDL_INIT_AUDIO)){std::cerr<<SDL_GetError();return 1;}
    int result=0;try{run();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    SDL_Quit();return result;
}
