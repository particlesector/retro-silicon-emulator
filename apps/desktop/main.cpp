// SPDX-License-Identifier: Apache-2.0
#include "application.hpp"
#include "audio_player.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <charconv>
#include <iostream>
#include <string_view>
#include <rse/session/builtins.hpp>
#include <algorithm>
#ifdef RSE_POLICY_ENABLED
#include <rse/session/policy_playback.hpp>
#endif
int main(int argc,char** argv) {
    unsigned seconds=0;bool audio=false,smoke=false;std::filesystem::path path;std::string_view system="nes-ntsc";
    DesktopLoad loader;
    DesktopFinish finish;
#ifdef RSE_POLICY_ENABLED
    std::filesystem::path model_path,replay_path;
    std::string_view provider="auto";
#endif
    if (argc==2 && std::string_view(argv[1])=="--help") {
        std::cout<<"Usage: rse-desktop [cartridge.nes] [--seconds <1..3600>]\n"
                 <<"       rse-desktop --audio cartridge.nes [--seconds <1..3600>]\n"
                 <<"       rse-desktop --smoke fixture.nes [--seconds <5..3600>]\n"
                 <<"       Optional: --system nes-ntsc (default)\n"
#ifdef RSE_POLICY_ENABLED
                 <<"       Optional: --model FILE --replay FILE [--provider cpu|cuda|auto]\n"
#endif
                 <<"O: Open  F5: Reset  Escape: Exit\n";return 0;
    }
    try {
        for (int i=1;i<argc;++i) {
            const std::string_view arg=argv[i];
            if ((arg=="--audio" || arg=="--smoke") && i==1) {audio=arg=="--audio";smoke=arg=="--smoke";}
            else if (arg=="--system" && i+1<argc) system=argv[++i];
#ifdef RSE_POLICY_ENABLED
            else if(arg=="--model" && i+1<argc && model_path.empty()) model_path=utf8_path(argv[++i]);
            else if(arg=="--replay" && i+1<argc && replay_path.empty()) replay_path=utf8_path(argv[++i]);
            else if(arg=="--provider" && i+1<argc) provider=argv[++i];
#endif
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
        const auto ids=rse::session::builtin_ids();
        if(std::find(ids.begin(),ids.end(),system)==ids.end())throw std::invalid_argument("Unknown built-in system: "+std::string(system));
        if(smoke && system!="nes-ntsc")throw std::invalid_argument("Smoke fixture requires nes-ntsc");
#ifdef RSE_POLICY_ENABLED
        if(!model_path.empty()) {
            if(path.empty() || replay_path.empty() || smoke || audio || system!="nes-ntsc")
                throw std::invalid_argument("Policy mode requires a Signal Run ROM and replay path");
            if(replay_path.extension()!=".resl" || std::filesystem::exists(replay_path))
                throw std::invalid_argument("Choose a new .resl replay path");
            auto model=std::make_shared<rse::policy::Model>(model_path,rse::policy::parse_provider(provider));
            loader=[model](const auto& cartridge) {
                const auto bytes=rse::policy::read_file(cartridge,64*1024);
                return std::make_unique<rse::policy::Playback>(model,bytes);
            };
            finish=[replay_path,model](const auto& session) {
                const auto& policy=dynamic_cast<const rse::policy::Playback&>(session);
                rse::policy::write_file(replay_path,policy.replay());
                std::cout<<"provider="<<(model->provider()==rse::policy::Provider::cuda?"cuda":"cpu")
                    <<" policy_completed="<<policy.completed()<<" manual="<<policy.manual()
                    <<" state_hash="<<std::hex<<rse::state::hash(policy.canonical_machine_state())<<'\n';
            };
        } else if(!replay_path.empty() || provider!="auto")
            throw std::invalid_argument("Replay and provider require a model");
#endif
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
    if (!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD)) {std::cerr<<SDL_GetError()<<'\n';return 1;}
    const int result=audio ? play_audio(path,seconds,system) :
        run_desktop(path,seconds,smoke,system,std::move(loader),std::move(finish));
    SDL_Quit();return result;
}
