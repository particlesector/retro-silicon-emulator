// SPDX-License-Identifier: Apache-2.0
#include "acceptance_programs.hpp"
#include <rse/systems/nes/machine.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
using namespace rse::nes;
using rse::Tick;
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
void reject(auto action) {
    try {action();}
    catch(const std::invalid_argument&) {return;}
    throw std::runtime_error("Invalid state accepted");
}
void round_trip(Machine& reference,Machine& restored,Tick end) {
    const auto captured=reference.snapshot();
    restored.restore(captured);
    check(restored.snapshot()==captured,"noncanonical restored snapshot");
    check(restored.frame().samples==reference.frame().samples &&
        restored.frame().number==reference.frame().number,"restored frame cursor");
    check(restored.trace().empty() && restored.take_audio().empty() && restored.drain().records.empty(),
        "restore retained host outputs");
    (void)reference.take_audio();
    const auto trace_begin=reference.trace().size();
    reference.advance_to(end);
    for(auto t=restored.state().tick+53;t<end;t+=53) restored.advance_to(t);
    restored.advance_to(end);
    check(reference.snapshot()==restored.snapshot(),"canonical state diverged under chunked replay");
    check(reference.take_audio()==restored.take_audio(),"PCM diverged after restore");
    check(reference.frame().samples==restored.frame().samples &&
        reference.frame().number==restored.frame().number,"published frame diverged after restore");
    check(std::ranges::equal(reference.trace().subspan(trace_begin),restored.trace()),
        "trace ordering diverged after restore");
    check(restored.drain().records.empty(),"restore left active inspection subscription");
}
void checkpoints() {
    const auto cartridge=rse::acceptance::av_input_program().cartridge();
    for(const auto profile:{Profile{.trace=true,.capture_audio=true},
        Profile{.ppu_phase=3,.cpu_phase=5,.dma_get_on_odd=false,.trace=true,.capture_audio=true}}) {
        Machine reference(cartridge,profile),restored(cartridge,profile);
        restored.subscribe({.capacity=16,.events=true,.boundary=rse::session::Boundary::master_tick});
        restored.advance_to(1000);
        round_trip(reference,restored,19); // Power-on, then halfway through reset.
        round_trip(reference,restored,96);
        check(!reference.state().instruction_boundary,"fixture missed mid-instruction");
        reference.schedule_input(120,0xff);
        reference.schedule_input(120,3);
        reference.schedule_irq(130,true);
        reference.schedule_irq(150,false);
        round_trip(reference,restored,200);
        check(reference.state().buttons==3,"equal tick input ordering");
        // Find DMC and OAM activity and checkpoint immediately.
        bool dmc=false,oam=false;
        const auto check_dma=[&](Tick tick) {
            Machine running(cartridge,profile);
            running.advance_to(tick);
            round_trip(running,restored,tick+128);
        };
        for(auto t=reference.state().tick+1;t<20'000 && !(dmc && oam);++t) {
            const auto first=reference.trace().size();
            reference.advance_to(t);
            for(const auto& event:reference.trace().subspan(first)) {
                if((event.kind==TraceKind::dmc_halt || event.kind==TraceKind::dmc_dummy) && !dmc) {
                    check_dma(t);
                    dmc=true;
                }
                if(event.kind==TraceKind::dma_get && !oam) {
                    check_dma(t);
                    oam=true;
                }
            }
        }
        check(dmc && oam,"fixture missed DMA checkpoint");
        round_trip(reference,restored,180'013);
        round_trip(reference,restored,400'001); // Drawn pixels, CHR RAM and frame in flight.
        check(reference.frame().number==1,"fixture missed frame publication");
        round_trip(reference,restored,750'000);
    }
}
void malformed() {
    auto cartridge=loop_program();
    Machine machine(cartridge,{.trace=true,.capture_audio=true});
    machine.advance_to(1500);
    const auto canonical=machine.snapshot();
    const auto trace_size=machine.trace().size();
    for(const auto index:{4u,8u,16u,24u,28u,60u}) {
        auto corrupt=canonical;
        corrupt.at(index)^=1;
        reject([&]{machine.restore(corrupt);});
    }
    // Repair integrity after changing the scheduler tick. Semantic validation
    // must still reject the clock state and keep the live timeline untouched.
    auto corrupt=canonical;
    corrupt[36]=0;
    corrupt[37]=0;
    const auto hash=rse::state::hash(std::span(corrupt).subspan(36));
    for(unsigned i=0;i<8;++i) corrupt[28+i]=static_cast<std::uint8_t>(hash>>(i*8));
    reject([&]{machine.restore(corrupt);});
    reject([&]{machine.restore(std::span(canonical).first(canonical.size()-1));});
    Machine other(rse::acceptance::av_input_program().cartridge(),{.capture_audio=true});
    reject([&]{other.restore(canonical);});
    Machine different(cartridge,{.cpu_phase=1,.capture_audio=true});
    reject([&]{different.restore(canonical);});
    check(machine.snapshot()==canonical && machine.trace().size()==trace_size &&
        !machine.take_audio().empty(),"failed restore mutated live state or outputs");
}
void interrupt_entry() {
    const auto cartridge=rse::acceptance::av_input_program().cartridge();
    const Profile profile{.trace=true,.capture_audio=true};
    Machine probe(cartridge,profile),reference(cartridge,profile),restored(cartridge,profile);
    probe.advance_to(400'000);
    const auto trace=probe.trace();
    const auto vector_read=std::ranges::find_if(trace,[](const auto& event) {
        return event.kind==TraceKind::cpu_read && event.address==0xfffa;
    });
    check(vector_read!=trace.end(),"fixture missed NMI entry");
    reference.advance_to(vector_read->tick);
    check(!reference.state().instruction_boundary,"NMI vector read completed interrupt entry early");
    round_trip(reference,restored,vector_read->tick+2048);
}
void write_bytes(const std::filesystem::path& path,std::span<const std::uint8_t> bytes) {
    std::ofstream file(path,std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    file.close();
    check(bool(file),"fixture write failed");
}
void replay(const std::filesystem::path& output) {
    auto cartridge=loop_program();
    Machine original(cartridge,{.trace=true}), replayed(cartridge,{.trace=true});
    using Kind=ReplayAction::Kind;
    const std::array<ReplayAction,4> actions{{
        {28,Kind::buttons,3},{28,Kind::buttons,4},{84,Kind::external_irq,1},{85,Kind::external_irq,0}}};
    const auto log=original.encode_replay(1000,actions);
    original.schedule_input(28,3);
    original.schedule_input(28,4);
    original.schedule_irq(84,true);
    original.schedule_irq(85,false);
    original.advance_to(1000);
    const auto canonical=original.snapshot();
    check(rse::state::hash(canonical)==0xf8cea44acd26916aull &&
        rse::state::hash(log)==0xa142cc82cb3bb289ull,"version 1 fixture bytes changed");
    replayed.advance_to(500);
    const auto target=replayed.load_replay(log);
    check(target==1000 && replayed.state().tick==0,"replay origin");
    replayed.advance_to(77);
    replayed.advance_to(target);
    check(replayed.snapshot()==canonical && std::ranges::equal(replayed.trace(),original.trace()),
        "portable log replay mismatch");
    auto corrupt=log;
    corrupt.back()^=1;
    reject([&]{(void)replayed.load_replay(corrupt);});
    check(replayed.state().tick==1000,"failed log load reset machine");

    NesProgram defective;
    defective.emit({0xea,0x02}); // Unsupported opcode on second opcode fetch.
    const auto bad_rom=defective.cartridge();
    Machine failing(bad_rom,{.trace=true});
    const auto reproduce=failing.encode_replay(130,actions);
    check(rse::state::hash(reproduce)==0x392081b82e0ef947ull,"version 1 failure log bytes changed");
    if(!output.empty()) {
        std::filesystem::create_directories(output);
        const auto image=rse::acceptance::image(defective);
        write_bytes(output/"unsupported-opcode.nes",image);
        write_bytes(output/"unsupported-opcode.resl",reproduce);
        write_bytes(output/"loop.resl",log);
        write_bytes(output/"loop.ress",canonical);
    }
    const auto run_failure=[&](Machine& machine,bool chunked) {
        const auto end=machine.load_replay(reproduce);
        try {
            if(chunked) machine.advance_to(100);
            machine.advance_to(end);
        } catch(const UnsupportedOpcode& error) {
            check(error.opcode==2 && error.address==0x8001 && machine.state().faulted,
                "unexpected replay failure");
            return machine.trace().back();
        }
        throw std::runtime_error("Failure log reached target without fault");
    };
    Machine repeated(bad_rom,{.trace=true});
    check(run_failure(failing,false)==run_failure(repeated,true),"fault replay boundary differed");
}
}
int main(int argc,char** argv) {
    try {
        check(argc==1 || argc==2,"Usage: rse-snapshot-tests [fixture-output-directory]");
        checkpoints();
        interrupt_entry();
        malformed();
        replay(argc==2 ? std::filesystem::path(argv[1]) : std::filesystem::path{});
    }
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
