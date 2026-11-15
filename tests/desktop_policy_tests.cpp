// SPDX-License-Identifier: Apache-2.0
#include "presentation.hpp"
#include "application.hpp"
#include "acceptance_programs.hpp"
#include <rse/systems/nes/machine.hpp>
#include <fstream>
#include <iostream>

namespace {
using namespace rse::desktop;
void check(bool ok,const char* message){if (!ok) throw std::runtime_error(message);}
void policy() {
    check(utf8_path("caf\xc3\xa9.nes").u8string()==u8"caf\u00e9.nes","UTF-8 cartridge path");
    check(viewport(960,720)==Viewport{0,0,960,720},"4:3 viewport");
    check(viewport(1000,480)==Viewport{180,0,640,480},"wide letterbox");
    check(viewport(480,720)==Viewport{0,180,480,360},"tall letterbox");
    check(viewport(0,0)==Viewport{},"minimized viewport");
    for(int w=1;w<1100;w+=7) for(int h=1;h<800;h+=11) {
        const auto v=viewport(w,h);check(v.x>=0 && v.y>=0 && v.x+v.w<=w && v.y+v.h<=h && v.w*3-v.h*4<4,"viewport bounds/ratio");
    }
    check(rgb(0x0f)==Rgb{0,0,0,255},"black color");
    check(rgb(0x20)==Rgb{248,248,248,255},"bright neutral palette entry");
    check(rgb(0x22)==Rgb{104,136,252,255},"blue sky palette entry");
    check(rgb(0x16)==Rgb{248,56,0,255},"red palette entry");
    check(rgb(0x1a)==Rgb{0,168,0,255},"green palette entry");
    for(unsigned level=0;level<4;++level) {
        const auto gray=rgb(level*16+13);check(gray.r==gray.g && gray.g==gray.b,"neutral gray palette column");
    }
    const auto normal=rgb(0x21),red=rgb(0x61);check(red.r==normal.r && red.g==normal.g*3/4 && red.b==normal.b*3/4,"emphasis conversion");
    for(unsigned i=0;i<512;++i) check(rgb(i).a==255,"opaque RGB output");
    Input input;input.key(1,true);input.gamepad(2);check(input.buttons()==3,"input OR");
    input.focus(false);check(input.buttons()==0,"focus release");input.key(8,true);input.focus(true);
    check(input.buttons()==2,"background key ignored");input.gamepad(0);check(input.buttons()==0,"gamepad disconnect");
    VideoTimeline video;rse::video::PpuFrame f;f.number=1;video.push(f,800);f.number=2;video.push(f,1600);
    check(!video.advance(799) && video.visible().number==0,"early video presentation");
    check(video.advance(800) && video.visible().number==1 && video.queued()==1,"matched PCM position");
    check(video.advance(2000) && video.visible().number==2 && video.queued()==0,"late presentation catches up");
    video.reset();check(video.visible().number==0 && video.queued()==0,"reset drops old video");
}
void replay() {
    auto p=rse::acceptance::av_input_program();const auto rom=p.cartridge();
    rse::nes::Machine a(rom,rse::nes::Profile{.capture_audio=true}),b(rom,rse::nes::Profile{.capture_audio=true});
    std::vector<std::int16_t> expected,actual;
    for(unsigned i=0;i<120;++i) {
        const std::uint8_t buttons=i<40?0:i<80?1:0;
        a.schedule_input(a.state().tick+1,buttons);b.schedule_input(b.state().tick+1,buttons);
        const auto& frame=a.run_frame();const auto target=a.state().tick;
        for(auto tick=b.state().tick+997;tick<target;tick+=997) b.advance_to(tick);
        b.advance_to(target);
        const auto ac=a.take_audio(),bc=b.take_audio();expected.insert(expected.end(),ac.begin(),ac.end());actual.insert(actual.end(),bc.begin(),bc.end());
        check(a.state().cpu==b.state().cpu && frame.samples==b.frame().samples,"host chunking changed desktop execution");
    }
    check(actual==expected && actual.size()>90000,"desktop PCM replay mismatch");
}
void write(const char* path) {
    auto p=rse::acceptance::av_input_program();(void)p.cartridge();std::array<std::uint8_t,16> header{'N','E','S',0x1a,1};
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(header.data()),header.size());
    out.write(reinterpret_cast<const char*>(p.prg.data()),p.prg.size());out.close();check(bool(out),"fixture write failed");
    header[6]=0x10;
    std::ofstream unsupported(std::string(path)+".unsupported.nes",std::ios::binary);
    unsupported.write(reinterpret_cast<const char*>(header.data()),header.size());
    unsupported.write(reinterpret_cast<const char*>(p.prg.data()),p.prg.size());unsupported.close();check(bool(unsupported),"unsupported fixture write failed");
    std::ofstream invalid(std::string(path)+".invalid.nes",std::ios::binary);invalid<<"bad";invalid.close();check(bool(invalid),"invalid fixture write failed");
}
}
int main(int argc,char** argv) {
    try {if(argc==3 && std::string_view(argv[1])=="--write-smoke") write(argv[2]);else {policy();replay();}}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
