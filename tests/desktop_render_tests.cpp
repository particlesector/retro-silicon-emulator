// SPDX-License-Identifier: Apache-2.0
#include "display.hpp"
#include "fake_machine_session.hpp"
#include <iostream>

int main() {
    if(!SDL_Init(SDL_INIT_VIDEO)){std::cerr<<SDL_GetError();return 1;}
    int result=0;
    try {
        std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Session render test",900,600,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
        rse::desktop::require_sdl(bool(window));
        rse::desktop::Display display(window.get());
        rse::testing::FakeMachineSession fake;
        fake.run_frame();
        rse::desktop::Frame frame{1,*fake.info().video,std::vector<rse::session::Rgba>(6)};
        fake.convert_video(frame.pixels);
        // Distinct pixels exercise generic source indexing as well as aspect.
        for(unsigned i=0;i<6;++i)frame.pixels[i]={std::uint8_t(i*37),std::uint8_t(255-i*23),71,255};
        display.draw(&frame,true);
        rse::desktop::require_sdl(SDL_SetWindowSize(window.get(),1000,480));
        SDL_PumpEvents();display.draw(&frame,true);
        frame={2,{5,1,{5,1},rse::session::PixelFormat::rgba8},std::vector<rse::session::Rgba>(5,{33,65,129,255})};
        display.draw(&frame,true); // Replacement recreates the texture.
        display.draw(nullptr);
        if(display.readbacks()!=3)throw std::runtime_error("Generic renderer readbacks missing");
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';result=1;}
    SDL_Quit();return result;
}
