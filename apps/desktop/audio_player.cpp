// SPDX-License-Identifier: Apache-2.0
#include "audio_player.hpp"
#include <rse/systems/nes/machine.hpp>
#include <SDL3/SDL.h>
#include <atomic>
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
struct Starvation {std::atomic<unsigned> requests{};};
void SDLCALL observe(void* userdata,SDL_AudioStream*,int additional,int) {
    if (additional>0) static_cast<Starvation*>(userdata)->requests.fetch_add(1,std::memory_order_relaxed);
}
void require(bool ok) {if (!ok) throw std::runtime_error(SDL_GetError());}
}
int play_audio(const std::filesystem::path& path,unsigned seconds) {
    try {
        rse::nes::Machine machine(rse::nes::Cartridge::load_file(path),rse::nes::Profile{.capture_audio=true});
        Starvation starvation;
        SDL_AudioSpec spec{SDL_AUDIO_S16,1,48000};
        std::unique_ptr<SDL_AudioStream,decltype(&SDL_DestroyAudioStream)> stream(
            SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,observe,&starvation),SDL_DestroyAudioStream);
        require(bool(stream));
        std::uint64_t hash=14695981039346656037ull,samples=0;
        unsigned peak=0;
        auto queued=[&] {const int n=SDL_GetAudioStreamQueued(stream.get());require(n>=0);return unsigned(n);};
        auto fill=[&] {
            // Bound source latency to roughly 120-137 ms. Host time controls only
            // how soon we request the next deterministic simulated block.
            // Cap work per event-loop iteration even on a host that cannot
            // synthesize in real time; starvation is then reported, not hidden.
            for (unsigned blocks=0;queued()<11520 && blocks<8;++blocks) {
                machine.advance_to(machine.state().tick+357960);
                const auto pcm=machine.take_audio();samples+=pcm.size();
                for (auto s:pcm) for (unsigned i=0;i<2;++i) hash=(hash^((std::uint16_t(s)>>(8*i))&255))*1099511628211ull;
                require(SDL_PutAudioStreamData(stream.get(),pcm.data(),static_cast<int>(pcm.size()*sizeof(std::int16_t))));
                peak=std::max(peak,queued());
            }
        };
        fill();require(SDL_ResumeAudioStreamDevice(stream.get()));
        const auto start=SDL_GetTicks();bool running=true;
        while (running && (!seconds || SDL_GetTicks()-start<std::uint64_t(seconds)*1000)) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) if (event.type==SDL_EVENT_QUIT ||
                (event.type==SDL_EVENT_KEY_DOWN && event.key.key==SDLK_ESCAPE)) running=false;
            fill();SDL_Delay(2);
        }
        require(SDL_PauseAudioStreamDevice(stream.get()));
        const auto elapsed=SDL_GetTicks()-start;
        const auto starved=starvation.requests.load(std::memory_order_relaxed);
        std::cout<<"Audio playback: driver="<<SDL_GetCurrentAudioDriver()<<" elapsed_ms="<<elapsed
                 <<" samples="<<samples<<" peak_queue_bytes="<<peak<<" starvation_requests="<<starved
                 <<" pcm_hash="<<std::hex<<hash<<std::dec<<'\n';
        // A bounded smoke run fails on any observed unsatisfied SDL request.
        return seconds && starved ? 1 : 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
