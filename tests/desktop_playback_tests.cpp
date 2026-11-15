// SPDX-License-Identifier: Apache-2.0
#include "playback.hpp"
#include "acceptance_programs.hpp"
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
void run() {
    std::atomic<unsigned> starvation{};
    SDL_AudioSpec spec{SDL_AUDIO_S16,1,48000};
    std::unique_ptr<SDL_AudioStream,decltype(&SDL_DestroyAudioStream)> audio(
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,observe,&starvation),SDL_DestroyAudioStream);
    check(bool(audio),"dummy audio device");
    const auto cartridge=rse::acceptance::av_input_program().cartridge();
    rse::desktop::Playback producer(audio.get());
    producer.replace(std::make_unique<rse::nes::Machine>(cartridge,rse::nes::Profile{.capture_audio=true}));
    producer.buttons(1);producer.active(true);wait([&]{return producer.ready();});producer.active(false);
    const auto primed=producer.state();
    rse::nes::Machine reference(cartridge,rse::nes::Profile{.capture_audio=true});
    reference.schedule_input(1,1);reference.advance_to(primed.tick);
    check(primed.cpu==reference.state().cpu && primed.cpu_cycles==reference.state().cpu_cycles,"producer changed CPU execution");
    for(unsigned address=0;address<2048;++address)
        check(producer.memory(address)==reference.inspect_memory(address),"producer changed RAM");
    check(producer.stats().samples==reference.take_audio().size(),"producer PCM clock mismatch");
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
    check(producer.tick()==0 && producer.memory(0)==0,"producer reset did not reconstruct machine");
    NesProgram bad;bad.emit({0x02});producer.replace(std::make_unique<rse::nes::Machine>(bad.cartridge(),rse::nes::Profile{.capture_audio=true}));
    producer.active(true);wait([&]{return !producer.error().empty();});producer.active(false);
    check(SDL_ClearAudioStream(audio.get()),"clear faulted stream");
    producer.replace(std::make_unique<rse::nes::Machine>(cartridge,rse::nes::Profile{.capture_audio=true}));
    producer.active(true);wait([&]{return producer.ready();});
    check(producer.error().empty() && producer.tick()>0,"producer did not recover after failed execution");
}
}
int main() {
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER,"dummy");
    if(!SDL_Init(SDL_INIT_AUDIO)){std::cerr<<SDL_GetError();return 1;}
    int result=0;try{run();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    SDL_Quit();return result;
}
