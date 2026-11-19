// SPDX-License-Identifier: Apache-2.0
#include "acceptance_programs.hpp"
#include "gameplay_program.hpp"
#include <rse/systems/nes/machine.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
using namespace rse::nes;
constexpr rse::Tick warmup=757'368, interval=714'736;
void check(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
void write(const std::filesystem::path& path,std::span<const std::uint8_t> bytes) {
    std::ofstream output(path,std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    output.close();
    check(bool(output),"gameplay fixture export failed");
}
void verify(const std::filesystem::path& output) {
    auto game=gameplay_program();
    Machine play(game.cartridge());
    play.advance_to(warmup);
    const auto starting_frame=play.frame().samples;
    for(unsigned pos=1;pos<=11;++pos) {
        play.schedule_input(play.state().tick+1,pos==3 || pos==7 ? 0x81 : 0x80);
        play.advance_to(play.state().tick+interval);
        check(play.inspect_memory(0x10)==pos && play.inspect_memory(0x11)==pos &&
            play.inspect_memory(0x13)==0,"scripted gameplay failed");
        check(play.inspect_memory(0x12)==std::uint8_t(pos==11),"finish gate incorrect");
        check(play.frame().samples!=starting_frame,"video did not reflect movement");
    }
    // An automated assertion detects that pressing right at the first hazard
    // incorrectly advances the defective ROM. The same input is safe in v1.
    const std::array<ReplayAction,1> actions{{{warmup+1,ReplayAction::Kind::buttons,0x80}}};
    const auto target=warmup+3*interval;
    auto defect=gameplay_program(true);
    Machine bad(defect.cartridge()),repeat(defect.cartridge()),healthy(game.cartridge());
    const auto log=bad.encode_replay(target,actions);
    bad.advance_to(bad.load_replay(log));
    const auto end=repeat.load_replay(log);
    repeat.advance_to(warmup+17);
    repeat.advance_to(end);
    healthy.advance_to(healthy.load_replay(healthy.encode_replay(target,actions)));
    check(healthy.inspect_memory(0x10)==2 && healthy.inspect_memory(0x13)==1,
        "healthy ROM failed obstacle assertion");
    check(bad.inspect_memory(0x10)==3 && bad.inspect_memory(0x13)==0 && bad.snapshot()==repeat.snapshot(),
        "seeded obstacle failure did not replay");
    if(!output.empty()) {
        std::filesystem::create_directories(output);
        write(output/"signal-run-v1.nes",rse::acceptance::image(game));
        write(output/"signal-run-defect-v1.nes",rse::acceptance::image(defect));
        write(output/"signal-run-obstacle-failure-v1.resl",log);
    }
}
}
int main(int argc,char** argv) {
    try {
        check(argc==1 || argc==2,"Usage: rse-gameplay-tests [output-directory]");
        verify(argc==2 ? std::filesystem::path(argv[1]) : std::filesystem::path{});
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
