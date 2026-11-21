// SPDX-License-Identifier: Apache-2.0
#include <rse/session/policy_playback.hpp>
#include <iostream>

int main(int argc,char** argv) {
    using namespace rse;
    try {
        if(argc!=3) throw std::invalid_argument("Expected a fixture ROM and bundle");
        const auto image=policy::read_file(argv[1],64*1024);
        auto model=std::make_shared<policy::Model>(argv[2],policy::Provider::cpu);
        policy::Playback play(model,image);
        const auto check=[](bool ok){if(!ok) throw std::runtime_error("Policy takeover or replay mismatch");};
        play.schedule_input(1,"controller-1",{});
        constexpr Tick warmup=757'368, interval=714'736;
        // Keep the host queue drained independently of scheduling boundaries.
        const auto advance=[&](Tick target) {
            while(play.status().tick<target) {
                play.advance_to(std::min(target,play.status().tick+25'001));
                (void)play.take_audio();
            }
        };
        advance(warmup+interval);
        check(!play.manual() && play.inspection()->peek("cpu.ram",0x10)==1);
        constexpr std::array<std::string_view,1> human{"a"};
        play.schedule_input(play.status().tick+1,"controller-1",human);
        advance(warmup+2*interval);
        check(play.manual() && play.inspection()->peek("cpu.ram",0x10)==1);
        play.schedule_input(play.status().tick+1,"controller-1",{});
        advance(warmup+3*interval);
        check(play.manual() && play.inspection()->peek("cpu.ram",0x10)==1);
        nes::Machine replay(nes::Cartridge::parse(image),{.capture_audio=true});
        const auto end=replay.load_replay(play.replay());
        for(Tick tick=12'345;tick<end;tick+=12'345) {
            replay.advance_to(tick);
            (void)replay.take_audio();
        }
        replay.advance_to(end);
        check(replay.snapshot()==play.canonical_machine_state());
        play.reset();
        check(!play.manual() && !play.completed() && play.status().tick==0);
        advance(warmup+interval);
        check(play.inspection()->peek("cpu.ram",0x10)==1);
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
