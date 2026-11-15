// SPDX-License-Identifier: Apache-2.0
#include "acceptance_programs.hpp"
#include <rse/systems/nes/machine.hpp>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>

namespace {
using namespace rse::nes;
void check(bool ok,const char* message){if(!ok) throw std::runtime_error(message);}
struct Hash {
    std::uint64_t value{14695981039346656037ull};
    void add(std::uint64_t n){for(unsigned i=0;i<8;++i){value=(value^(n&255))*1099511628211ull;n>>=8;}}
};
struct Observation {
    std::uint64_t tick{},samples{},state{},events{},frames{},pcm{};
    bool operator==(const Observation&)const=default;
};
std::uint64_t state_hash(const Machine& machine) {
    Hash h;const auto s=machine.state();const auto a=machine.apu_state();
    for(std::uint64_t n:{s.tick,s.cpu_cycles,std::uint64_t(s.cpu.pc),std::uint64_t(s.cpu.s),
        std::uint64_t(s.cpu.a),std::uint64_t(s.cpu.x),std::uint64_t(s.cpu.y),std::uint64_t(s.cpu.p),
        s.ppu.dots,s.ppu.frames,std::uint64_t(s.ppu.scanline),std::uint64_t(s.ppu.dot),
        std::uint64_t(s.ppu.v),std::uint64_t(s.ppu.t),std::uint64_t(s.ppu.status),
        a.cycles,a.quarters,a.halves,std::uint64_t(a.frame_cycle),
        std::uint64_t(a.dmc.address),std::uint64_t(a.dmc.remaining),std::uint64_t(a.dmc.output)}) h.add(n);
    for(unsigned i=0;i<2048;++i)h.add(*machine.inspect_memory(static_cast<std::uint16_t>(i)));
    return h.value;
}
Observation run(Machine& machine,bool chunked,const std::array<rse::Tick,12>* targets=nullptr,
                std::array<rse::Tick,12>* recorded=nullptr) {
    Hash frames,pcm,events;std::uint64_t samples=0;
    for(unsigned frame=0;frame<12;++frame) {
        machine.schedule_input(machine.state().tick+1,frame>=4 && frame<8 ? 1 : 0);
        if(chunked) {
            const auto target=targets->at(frame);
            for(auto t=machine.state().tick+997;t<target;t+=997)machine.advance_to(t);
            machine.advance_to(target);
        } else (void)machine.run_frame();
        if(recorded)recorded->at(frame)=machine.state().tick;
        frames.add(machine.frame().number);
        for(auto sample:machine.frame().samples)frames.add(sample);
        const auto audio=machine.take_audio();samples+=audio.size();
        for(auto sample:audio)pcm.add(static_cast<std::uint16_t>(sample));
    }
    for(auto e:machine.trace()){events.add(e.tick);events.add(unsigned(e.kind));events.add(e.address);events.add(e.data);events.add(e.cycle);}
    check(samples==machine.state().cpu_cycles*48000ull*11/19687500,"acceptance sample count");
    check(machine.frame().number==12,"acceptance frame count");
    return {machine.state().tick,samples,state_hash(machine),events.value,frames.value,pcm.value};
}
void write_set(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    for(auto [name,program]:rse::acceptance::programs()) {
        const auto bytes=rse::acceptance::image(std::move(program));
        std::ofstream out(folder/std::string(name),std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());out.close();check(bool(out),"acceptance ROM write failed");
    }
    // Same companions used by the desktop error-recovery script.
    auto bytes=rse::acceptance::image(rse::acceptance::av_input_program());bytes[6]=0x10;
    std::ofstream unsupported(folder/"rse-av-input-v1.nes.unsupported.nes",std::ios::binary);
    unsupported.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());unsupported.close();check(bool(unsupported),"unsupported fixture write failed");
    std::ofstream invalid(folder/"rse-av-input-v1.nes.invalid.nes",std::ios::binary);invalid<<"bad";invalid.close();check(bool(invalid),"invalid fixture write failed");
}
void verify(bool print) {
    // Regression captures frozen from the M7 programs, separate from chip oracles.
    constexpr std::array<Observation,3> expected{{
        {4261144,9523,0xed9040d64cb18b54ull,0x7e5d9b9ddad39bd7ull,0xf83104773c052829ull,0xd16759ef6b96e105ull},
        {4261144,9523,0x28f7f81e71767fb4ull,0x418e4c763e6e040cull,0xf83104773c052829ull,0xa4d96b23b4cf5dbbull},
        {4261120,9523,0x50c358ce8d7c45eaull,0xe25fdc508fb80fbbull,0x81db4b05c9783829ull,0xa4d96b23b4cf5dbbull}}};
    unsigned index=0;
    for(auto [name,program]:rse::acceptance::programs()) {
        const auto cartridge=Cartridge::parse(rse::acceptance::image(std::move(program)));
        const Profile profile{.trace=true,.trace_limit=2'000'000,.capture_audio=true};
        Machine baseline(cartridge,profile);std::array<rse::Tick,12> targets;
        const auto actual=run(baseline,false,nullptr,&targets);
        if(print) {
            std::cout<<name<<" tick="<<std::dec<<actual.tick<<" samples="<<actual.samples<<std::hex
                <<" state="<<actual.state<<" events="<<actual.events<<" frames="<<actual.frames<<" pcm="<<actual.pcm<<'\n';continue;
        }
        if(actual!=expected[index++]) throw std::runtime_error(std::string(name)+": pinned observation mismatch");
        Machine replay(cartridge,profile);check(run(replay,true,&targets)==actual,"acceptance chunk replay mismatch");
        baseline.reset();check(run(baseline,false)==actual,"acceptance reset replay mismatch");
        if(name=="rse-av-input-v1.nes") {
            check(baseline.inspect_memory(0)==11 && baseline.inspect_memory(0x10)==0,"acceptance NMI/input result");
            check(std::all_of(baseline.frame().samples.begin(),baseline.frame().samples.end(),[](auto s){return s==0x21;}),"acceptance video result");
        }
    }
}
}
int main(int argc,char** argv) {
    try {
        if(argc==3 && std::string_view(argv[1])=="--write-set")write_set(argv[2]);
        else if(argc==1 || (argc==2 && std::string_view(argv[1])=="--print-baseline"))verify(argc==2);
        else throw std::invalid_argument("Usage: rse-acceptance-tests [--write-set <folder>|--print-baseline]");
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
