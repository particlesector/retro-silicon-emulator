// SPDX-License-Identifier: Apache-2.0
#include "nes_programs.hpp"
#include <rse/systems/nes/machine.hpp>
#include <rse/audio/pcm.hpp>
#include <fstream>
#include <iostream>

namespace {
using namespace rse::nes;
void check(bool ok,const char* message) {if (!ok) throw std::runtime_error(message);}
std::uint64_t hash(std::span<const std::int16_t> pcm) {
    std::uint64_t h=14695981039346656037ull;
    for (auto s:pcm) for (unsigned i=0;i<2;++i) {h=(h^((std::uint16_t(s)>>(8*i))&255))*1099511628211ull;}
    return h;
}
void repeat(bool print) {
    auto program=audio_program();const auto cartridge=program.cartridge();
    Machine baseline(cartridge,Profile{.trace=true,.capture_audio=true});
    constexpr rse::Tick target=2'000'000;
    baseline.advance_to(target);const auto expected=baseline.take_audio();
    if (print) {std::cout<<"samples "<<expected.size()<<" hash "<<std::hex<<hash(expected)<<'\n';return;}
    check(expected.size()==4469,"PCM rational sample count");
    // Fixed regression capture; channel timing is checked independently in apu-tests.
    check(hash(expected)==0x21e6c536d9b67d48ull,"PCM golden mismatch");
    check(std::any_of(expected.begin(),expected.end(),[](auto s){return s>1000;}),"silent PCM fixture");
    for (unsigned run=0;run<3;++run) {
        Machine replay(cartridge,Profile{.trace=true,.capture_audio=true});
        std::vector<std::int16_t> actual;
        for (rse::Tick end=997;end<target;end+=997) {
            replay.advance_to(end);
            if ((end%5)==0) {auto block=replay.take_audio();actual.insert(actual.end(),block.begin(),block.end());}
        }
        replay.advance_to(target);auto tail=replay.take_audio();actual.insert(actual.end(),tail.begin(),tail.end());
        check(actual==expected,"PCM chunk/drain mismatch");
        check(replay.state().cpu==baseline.state().cpu && replay.state().ppu.dots==baseline.state().ppu.dots,
              "PCM changed machine execution");
        check(std::equal(replay.trace().begin(),replay.trace().end(),baseline.trace().begin(),baseline.trace().end()),"audio trace repeat mismatch");
        replay.reset();replay.advance_to(target);check(replay.take_audio()==expected,"PCM reset mismatch");
    }
    Machine discarded(cartridge);discarded.advance_to(target);
    check(discarded.state().cpu==baseline.state().cpu && discarded.take_audio().empty(),"capture changed execution");
    Machine bounded(cartridge,Profile{.capture_audio=true,.audio_limit=1});
    try {bounded.advance_to(1000);throw std::runtime_error("audio overflow not rejected");}catch(const std::length_error&) {}
    check(bounded.state().faulted,"audio overflow did not fault");bounded.reset();check(bounded.take_audio().empty(),"audio reset queue");
    unsigned oam_get=0,oam_put=0,dmc_get=0;
    for (auto e:baseline.trace()) {
        if (e.kind==TraceKind::dma_get) {check(e.address==0x200+oam_get,"integrated OAM sequence");++oam_get;}
        if (e.kind==TraceKind::dma_put) ++oam_put;
        if (e.kind==TraceKind::dmc_get) {check(e.address>=0xc800 && e.address<=0xc810,"integrated DMC address");++dmc_get;}
    }
    check(oam_get==256 && oam_put==256 && dmc_get>17,"integrated looping DMC/OAM did not complete");
}
void irqs() {
    NesProgram p;p.emit({0x58});p.label("idle");p.jump(0x4c,"idle");
    p.at(0x9100);p.emit({0x48,0xad,0x15,0x40,0x85,1,0xe6,0,0x68,0x40});
    Machine frame(p.cartridge(),Profile{.trace=true});frame.advance_to(400000);
    check(frame.inspect_memory(0)==1 && frame.inspect_memory(1)==0x40 && !frame.apu_state().frame_irq,"frame IRQ CPU wiring/read acknowledgement");
    p=NesProgram{};
    p.emit({0xa9,0x40,0x8d,0x17,0x40,0xa9,0x8f,0x8d,0x10,0x40,
            0xa9,0x10,0x8d,0x15,0x40,0x58});p.label("idle");p.jump(0x4c,"idle");
    p.at(0x9100);p.emit({0x48,0xad,0x15,0x40,0x85,1,0xa9,0,0x8d,0x15,0x40,0xe6,0,0x68,0x40});
    Machine dmc(p.cartridge(),Profile{.trace=true});dmc.advance_to(3000);
    check(dmc.inspect_memory(0)==1 && dmc.inspect_memory(1)==0x80 && !dmc.apu_state().dmc.irq,"DMC IRQ CPU wiring/write acknowledgement");
    check(dmc.apu_state().cycles==dmc.state().cpu_cycles,"APU stopped during DMC hold");
}
void write_smoke(const char* filename) {
    auto p=audio_program();(void)p.cartridge();
    std::array<std::uint8_t,16> header{'N','E','S',0x1a,1};
    std::ofstream out(filename,std::ios::binary);out.write(reinterpret_cast<const char*>(header.data()),header.size());
    out.write(reinterpret_cast<const char*>(p.prg.data()),p.prg.size());out.close();check(bool(out),"smoke fixture write failed");
}
}
int main(int argc,char** argv) {
    try {
        if (argc==3 && std::string_view(argv[1])=="--write-smoke") {write_smoke(argv[2]);return 0;}
        repeat(argc==2 && std::string_view(argv[1])=="--print-golden");
        if (argc==1) irqs();
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
