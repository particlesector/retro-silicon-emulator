// SPDX-License-Identifier: Apache-2.0
#include "trainer.hpp"
#include <SDL3/SDL.h>
#include <fstream>
#include <iostream>

namespace {
using namespace rse::desktop;
void check(bool good,const char* message) {if(!good) throw std::runtime_error(message);}
std::string_view option(int argc,char** argv,std::string_view key) {
    for(int i=1;i+1<argc;++i) if(argv[i]==key) return argv[i+1];
    return {};
}
int child(int argc,char** argv) {
    if(option(argc,argv,"--observation")=="screen") return 42;
    const auto output=std::filesystem::path(option(argc,argv,"--out"));
    const auto stop=std::filesystem::path(option(argc,argv,"--stop-file"));
    std::filesystem::create_directory(output);
    std::ofstream(output/"live.txt")<<"RSE19 64 100.5 1 1.25 1 0.125 2\n";
    const std::string frame(256*240*3,static_cast<char>(0x40));
    std::ofstream(output/"preview.rgb",std::ios::binary).write(frame.data(),frame.size());
    if(option(argc,argv,"--resume").empty()) {
        const auto deadline=SDL_GetTicks()+10'000;
        while(!std::filesystem::exists(stop) && SDL_GetTicks()<deadline) SDL_Delay(10);
        if(!std::filesystem::exists(stop)) return 43;
    }
    std::ofstream(output/"checkpoint.zip")<<"fixture";
    return 0;
}
void wait(Trainer& trainer,auto done) {
    const auto deadline=SDL_GetTicks()+7'000;
    while(!done() && SDL_GetTicks()<deadline) {
        (void)trainer.poll();
        SDL_Delay(15);
    }
    check(done(),"trainer test deadline");
}
}
int main(int argc,char** argv) {
    try {
        if(argc>2) return child(argc,argv);
        check(argc==2,"Expected trainer output directory");
        const auto root=std::filesystem::path(argv[1])/std::to_string(SDL_GetPerformanceCounter());
        Trainer trainer({.python=std::filesystem::absolute(argv[0]),.script="fixture script",
                         .bridge="fixture bridge",.output=root});
        trainer.start("fixture rom","fixture profile","memory");
        wait(trainer,[&]{return trainer.preview().number && trainer.metrics().steps==64;});
        check(trainer.running() && trainer.metrics().reward==1.25 && trainer.metrics().loss==0.125 &&
              trainer.metrics().rate==100.5 && trainer.metrics().action==2 &&
              trainer.preview().pixels.front().r==0x40,"bad telemetry");
        trainer.stop();
        wait(trainer,[&]{return !trainer.running();});
        check(trainer.status()=="Stopped at rollout boundary" && !trainer.checkpoint().empty(),"bad stop");
        const auto checkpoint=trainer.checkpoint();
        trainer.start("fixture rom","fixture profile","screen");
        wait(trainer,[&]{return !trainer.running();});
        check(trainer.status().find("exit 42")!=std::string::npos && trainer.checkpoint()==checkpoint,
            "disconnected run lost the previous checkpoint");
        trainer.start("fixture rom","fixture profile","memory",checkpoint);
        wait(trainer,[&]{return !trainer.running();});
        check(trainer.status()=="Training completed" && trainer.checkpoint()!=checkpoint,"bad resume");
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
