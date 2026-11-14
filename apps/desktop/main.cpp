// SPDX-License-Identifier: Apache-2.0
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
#include <iostream>
#include <charconv>
#include <string_view>
#include "audio_player.hpp"
int main(int argc,char** argv) {
    unsigned seconds=0;
    const bool audio=argc>=2 && std::string_view(argv[1])=="--audio";
    if (argc==2 && std::string_view(argv[1])=="--help") {
        std::cout<<"Usage: rse-desktop [--audio <rom.nes> [--seconds <1..3600>]]\n";return 0;
    }
    if (argc!=1 && !(audio && (argc==3 || argc==5))) {
        std::cerr<<"Invalid arguments. Use --help.\n";return 2;
    }
    if (argc==5) {
        const std::string_view number=argv[4];
        const auto result=std::from_chars(number.data(),number.data()+number.size(),seconds);
        if (std::string_view(argv[3])!="--seconds" || result.ec!=std::errc{} ||
            result.ptr!=number.data()+number.size() || !seconds || seconds>3600) {
            std::cerr<<"Seconds must be an integer from 1 to 3600.\n";return 2;
        }
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        std::cerr << SDL_GetError() << '\n';
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow(audio ? "Retro Silicon Emulator - audio playback" : "Retro Silicon Emulator (scaffold)", 960, 720, SDL_WINDOW_VULKAN);
    if (!window) {
        std::cerr << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }
    if (audio) {
        const int result=play_audio(argv[2],seconds);
        SDL_DestroyWindow(window);SDL_Quit();return result;
    }
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
        }
        SDL_Delay(16);
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
