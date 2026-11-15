// SPDX-License-Identifier: Apache-2.0
#include "application.hpp"
#include "audio_player.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <charconv>
#include <iostream>
#include <string_view>
int main(int argc,char** argv) {
    unsigned seconds=0;bool audio=false,smoke=false;std::filesystem::path path;
    if (argc==2 && std::string_view(argv[1])=="--help") {
        std::cout<<"Usage: rse-desktop [cartridge.nes] [--seconds <1..3600>]\n"
                 <<"       rse-desktop --audio cartridge.nes [--seconds <1..3600>]\n"
                 <<"       rse-desktop --smoke fixture.nes [--seconds <5..3600>]\n"
                 <<"O: Open  F5: Reset  Escape: Exit\n";return 0;
    }
    try {
        for (int i=1;i<argc;++i) {
            const std::string_view arg=argv[i];
            if ((arg=="--audio" || arg=="--smoke") && i==1) {audio=arg=="--audio";smoke=arg=="--smoke";}
            else if (arg=="--seconds" && i+1<argc && !seconds) {
                const std::string_view value=argv[++i];
                const auto result=std::from_chars(value.data(),value.data()+value.size(),seconds);
                if (result.ec!=std::errc{} || result.ptr!=value.data()+value.size() || !seconds || seconds>3600)
                    throw std::invalid_argument("Seconds must be an integer from 1 to 3600");
            } else if (path.empty() && !arg.starts_with("--")) path=utf8_path(arg);
            else throw std::invalid_argument("Invalid arguments. Use --help.");
        }
        if ((audio || smoke) && path.empty()) throw std::invalid_argument("A cartridge path is required");
        if (smoke && !seconds) seconds=30;
        if (smoke && seconds<5) throw std::invalid_argument("Smoke duration must be at least 5 seconds");
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
    if (!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD|SDL_INIT_AUDIO)) {std::cerr<<SDL_GetError()<<'\n';return 1;}
    const int result=audio ? play_audio(path,seconds) : run_desktop(path,seconds,smoke);
    SDL_Quit();return result;
}
