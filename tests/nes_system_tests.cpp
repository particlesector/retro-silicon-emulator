// SPDX-License-Identifier: Apache-2.0
#include "nes_programs.hpp"
#include <rse/systems/nes/machine.hpp>
#include <algorithm>
#include <array>
#include <iomanip>
#include <iostream>
#include <limits>

namespace {
using namespace rse::nes;
void check(bool condition,const std::string& message) { if (!condition) throw std::runtime_error(message); }
template<class Exception,class Action> void rejects(Action action) {
    try { action(); } catch(const Exception&) { return; }
    throw std::runtime_error("expected rejection");
}
struct Hash {
    std::uint64_t value{14695981039346656037ull};
    void add(std::uint64_t number) { for (unsigned i=0;i<8;++i) { value=(value^(number&255))*1099511628211ull; number>>=8; } }
};
std::uint64_t event_hash(std::span<const TraceEvent> events) {
    Hash hash;
    for (const auto& e:events) { hash.add(e.tick); hash.add(unsigned(e.kind)); hash.add(e.address); hash.add(e.data); hash.add(e.cycle); }
    return hash.value;
}
std::uint64_t frame_hash(const rse::video::PpuFrame& frame) {
    Hash hash; for (auto sample:frame.samples) hash.add(sample); return hash.value;
}
std::uint64_t observation_hash(const MachineState& s,const std::array<std::uint8_t,2048>& ram) {
    Hash h;
    for (std::uint64_t value : {s.tick,s.cpu_cycles,std::uint64_t(s.cpu.pc),std::uint64_t(s.cpu.s),
        std::uint64_t(s.cpu.a),std::uint64_t(s.cpu.x),std::uint64_t(s.cpu.y),std::uint64_t(s.cpu.p),
        std::uint64_t(s.ppu.scanline),std::uint64_t(s.ppu.dot),s.ppu.dots,s.ppu.frames,
        std::uint64_t(s.ppu.v),std::uint64_t(s.ppu.t),std::uint64_t(s.ppu.fine_x),
        std::uint64_t(s.ppu.control),std::uint64_t(s.ppu.mask),std::uint64_t(s.ppu.status),
        std::uint64_t(s.ppu.oam_address),std::uint64_t(s.ppu.write_toggle),std::uint64_t(s.ppu.odd),
        std::uint64_t(s.ppu.nmi),std::uint64_t(s.ppu.faulted),std::uint64_t(s.bus_latch),std::uint64_t(s.buttons),
        std::uint64_t(s.controller.parallel),std::uint64_t(s.controller.shift),std::uint64_t(s.controller.load),
        std::uint64_t(s.controller.serial),std::uint64_t(s.dma.pending),std::uint64_t(s.dma.active),
        std::uint64_t(s.dma.page),std::uint64_t(s.dma.data),std::uint64_t(s.dma.index),
        std::uint64_t(s.instruction_boundary),std::uint64_t(s.irq),std::uint64_t(s.nmi),std::uint64_t(s.faulted)}) h.add(value);
    for (auto byte:ram) h.add(byte);
    return h.value;
}
std::array<std::uint8_t,2048> ram(const Machine& machine) {
    std::array<std::uint8_t,2048> result{};
    for (unsigned i=0;i<result.size();++i) result[i]=*machine.inspect_memory(static_cast<std::uint16_t>(i));
    return result;
}
void timeline(Machine& machine) {
    // Deliberately enqueue out of time order, including stable equal-time events.
    machine.schedule_input(600000,0x80); machine.schedule_input(1,0x81);
    machine.schedule_irq(100,true); machine.schedule_input(500,2); machine.schedule_input(500,0xa5);
    machine.schedule_irq(1000,false); machine.schedule_input(200000,0x18);
}
constexpr rse::Tick loop_target=720089;
std::vector<TraceEvent> loop_oracle() {
    // Hand-derived bus trace for reset(7), then INC $10(5) / JMP $8000(3).
    // No rendering/PPU register accesses, so only frame publications add events.
    const std::vector<TraceEvent> other{
        {1,TraceKind::input,0,0x81,0},{100,TraceKind::irq,0,1,0},
        {500,TraceKind::input,0,2,0},{500,TraceKind::input,0,0xa5,0},
        {1000,TraceKind::irq,0,0,0},{200000,TraceKind::input,0,0x18,0},
        {330096,TraceKind::frame,0,1,0},{600000,TraceKind::input,0,0x80,0},
        {687464,TraceKind::frame,0,2,0}};
    const std::array<unsigned,7> reset_addresses{0,0,0x100,0x1ff,0x1fe,0xfffc,0xfffd};
    std::vector<TraceEvent> result;
    std::size_t next=0;
    for (std::uint64_t cycle=1;cycle<=60007;++cycle) {
        const auto tick=cycle*12;
        while (next<other.size() && other[next].tick<=tick) result.push_back(other[next++]);
        TraceEvent e{tick,TraceKind::cpu_read,0,0,cycle};
        if (cycle<=7) { e.address=static_cast<std::uint16_t>(reset_addresses[cycle-1]); e.data=cycle==7 ? 0x80 : 0; }
        else {
            const auto iteration=(cycle-8)/8;
            switch ((cycle-8)%8) {
            case 0:e.address=0x8000; e.data=0xe6; break;
            case 1:e.address=0x8001; e.data=0x10; break;
            case 2:e.address=0x10; e.data=iteration&255; break;
            case 3:e.address=0x10; e.data=iteration&255; e.kind=TraceKind::cpu_write; break;
            case 4:e.address=0x10; e.data=(iteration+1)&255; e.kind=TraceKind::cpu_write; break;
            case 5:e.address=0x8002; e.data=0x4c; break;
            case 6:e.address=0x8003; e.data=0; break;
            case 7:e.address=0x8004; e.data=0x80; break;
            }
        }
        result.push_back(e);
    }
    while (next<other.size()) result.push_back(other[next++]);
    return result;
}
std::uint64_t loop_state_oracle() {
    MachineState s{};
    s.tick=loop_target; s.cpu_cycles=60007; s.cpu={0x8000,0xfd,0,0,0,0x24};
    s.ppu.scanline=2; s.ppu.dot=315; s.ppu.dots=180022; s.ppu.frames=2;
    s.bus_latch=0x80; s.buttons=0x80; s.controller={0xfe,0xff,false,false}; s.instruction_boundary=true;
    std::array<std::uint8_t,2048> memory{}; memory[0x10]=76;
    return observation_hash(s,memory);
}
// Populated from --print-goldens, which runs only the independent loop oracle.
constexpr std::uint64_t golden_loop_events=0xfc34a569a9f0d2ebull;
constexpr std::uint64_t golden_loop_state=0x69f1cb6cc7e11967ull;
constexpr std::uint64_t golden_blank_frame=0xcf28946e7ff82325ull;
void compare_events(std::span<const TraceEvent> actual,std::span<const TraceEvent> expected) {
    check(actual.size()==expected.size(),"loop trace length differs: "+std::to_string(actual.size())+" vs "+std::to_string(expected.size()));
    for (std::size_t i=0;i<expected.size();++i) {
        if (actual[i]==expected[i]) continue;
        throw std::runtime_error("first loop trace mismatch at event "+std::to_string(i)+
            ": tick "+std::to_string(actual[i].tick)+" kind "+std::to_string(unsigned(actual[i].kind))+
            " address "+std::to_string(actual[i].address)+" data "+std::to_string(actual[i].data)+
            "; expected tick "+std::to_string(expected[i].tick)+" kind "+std::to_string(unsigned(expected[i].kind))+
            " address "+std::to_string(expected[i].address)+" data "+std::to_string(expected[i].data));
    }
}
void deterministic_loop() {
    const auto expected=loop_oracle();
    check(event_hash(expected)==golden_loop_events && loop_state_oracle()==golden_loop_state,"loop oracle/golden mismatch");
    Machine machine(loop_program(),Profile{.trace=true});
    for (unsigned replay=0;replay<3;++replay) {
        if (replay) machine.reset();
        timeline(machine);
        if (replay!=1) machine.advance_to(loop_target);
        else {
            const std::array<unsigned,6> chunks{1,17,4,63,512,10000};
            for (unsigned i=0;machine.state().tick<loop_target;++i)
                machine.advance_to(std::min(loop_target,machine.state().tick+chunks[i%chunks.size()]));
        }
        compare_events(machine.trace(),expected);
        check(event_hash(machine.trace())==golden_loop_events,"loop event checksum");
        check(observation_hash(machine.state(),ram(machine))==golden_loop_state,"loop state checksum");
        check(frame_hash(machine.frame())==golden_blank_frame && machine.frame().number==2,"loop frame checksum/count");
    }
    Machine no_trace(loop_program()); timeline(no_trace); no_trace.advance_to(loop_target);
    check(no_trace.trace().empty() && observation_hash(no_trace.state(),ram(no_trace))==golden_loop_state,
          "tracing changed execution");
}
void clocks_and_failures() {
    for (std::uint8_t ppu_phase:{0,1,3}) for (std::uint8_t cpu_phase:{0,1,11}) {
        Machine machine(loop_program(),Profile{.ppu_phase=ppu_phase,.cpu_phase=cpu_phase});
        machine.advance_to(1001);
        const auto pfirst=ppu_phase ? ppu_phase : 4, cfirst=cpu_phase ? cpu_phase : 12;
        check(machine.state().ppu.dots==1+(1001-pfirst)/4 && machine.state().cpu_cycles==1+(1001-cfirst)/12,
              "machine oscillator ratios/phases");
        machine.advance_to(1001);
        rejects<std::invalid_argument>([&] { machine.advance_to(1000); });
        rejects<std::invalid_argument>([&] { machine.schedule_input(1001,0); });
        check(!machine.state().faulted,"invalid input should not fault execution");
        machine.schedule_input(1002,1); machine.advance_to(1002);
        machine.schedule_input(1003,2); machine.advance_to(1003);
        check(machine.state().buttons==2,"scheduling after queue compaction");
        check(!machine.inspect_memory(0x2002) && !machine.inspect_memory(0x4016),"inspection must not expose side-effecting reads");
    }
    rejects<std::invalid_argument>([] { Machine invalid(loop_program(),Profile{.ppu_phase=4}); });
    rejects<std::invalid_argument>([] { Machine invalid(loop_program(),Profile{.cpu_phase=12}); });
    rejects<std::invalid_argument>([] { Machine invalid(loop_program(),Profile{.trace=true,.trace_limit=0}); });
    Machine bounded(loop_program(),Profile{.trace=true,.trace_limit=1});
    rejects<std::length_error>([&] { bounded.advance_to(100); });
    check(bounded.state().faulted && bounded.trace().size()==1,"trace bound/fault");
    rejects<std::logic_error>([&] { bounded.advance_to(100); });
    bounded.reset(); check(!bounded.state().faulted && bounded.state().tick==0 && bounded.trace().empty(),"reset must recover/clear timeline");
    NesProgram bad; bad.emit({2}); Machine unsupported(bad.cartridge());
    rejects<UnsupportedOpcode>([&] { unsupported.advance_to(120); });
    check(unsupported.state().faulted,"CPU exception must fault the machine");
    rejects<std::logic_error>([&] { unsupported.run_frame(); });
    unsupported.reset(); check(unsupported.state().cpu_cycles==0 && !unsupported.state().faulted,"CPU fault reset recovery");
}
void memory_decode() {
    NesProgram p;
    p.emit({0xa9,0x20,0x8d,0xfe,0x3f,0xa9,0,0x8d,0xfe,0x3f,0xa9,0x77,0x8d,0xff,0x3f,
            0xa9,0x20,0x8d,6,0x20,0xa9,0,0x8d,6,0x20,0xad,7,0x20,0xad,0xff,0x3f,0x85,0x20,
            0xa9,0xfc,0x8d,0xfb,0x3f,0xa9,0x42,0x8d,0xfc,0x3f,0xa9,0xfc,0x8d,3,0x20,
            0xad,4,0x20,0x85,0x21,0xad,0x17,0x40,0x85,0x22,0xad,0,0x60,0x85,0x23,
            0xa9,0x55,0x8d,0,0x80,0xad,0,0x80,0x85,0x24,
            0xa9,0x5a,0x8d,0x10,0x18,0xad,0x10,8,0x85,0x25});
    p.label("idle"); p.jump(0x4c,"idle");
    Machine machine(p.cartridge()); machine.advance_to(3000);
    for (auto [address,value]:{std::pair{0x20,0x77},std::pair{0x21,0x42},std::pair{0x22,0x40},
                              std::pair{0x23,0x60},std::pair{0x24,0xa9},std::pair{0x25,0x5a}})
        check(machine.inspect_memory(static_cast<std::uint16_t>(address))==value,"CPU decode/mirroring/open-bus reference");
    check(machine.inspect_memory(0x8000)==0xa9,"CPU ROM writes must not change program data");
}
void register_phase_order() {
    for (std::uint8_t phase:{0,11}) {
        NesProgram p;
        p.emit({0x4c,3,0x80});
        for (unsigned i=0;i<13747;++i) p.emit({0xea});
        p.emit({0xad,2,0x20,0x85,0}); p.label("idle"); p.jump(0x4c,"idle");
        Machine machine(p.cartridge(),Profile{.cpu_phase=phase,.trace=true});
        machine.advance_to(330144);
        check(machine.inspect_memory(0)==(phase==0 ? 0x80 : 0),"CPU/PPU coincident edge/status suppression ordering");
        const auto read=std::find_if(machine.trace().begin(),machine.trace().end(),[](const auto& event) {
            return event.kind==TraceKind::cpu_read && event.address==0x2002;
        });
        check(read!=machine.trace().end() && read->tick==(phase==0 ? 330096 : 330095) && read->cycle==27508,
              "fixed-cycle status-read reference");
        check(machine.frame().number==1,"status suppression must not suppress frame publication");
    }
}
void video_and_input() {
    Machine machine(video_input_program(),Profile{.trace=true});
    machine.schedule_input(1,0x81); machine.schedule_input(330000,0xa5); machine.schedule_input(330800,0x3c);
    machine.advance_to(334000);
    check(machine.inspect_memory(0)==1 && machine.inspect_memory(0x10)==0xa5,"first NMI/controller snapshot");
    check(machine.run_frame().number==2 && machine.state().tick==687460,"rendered odd-frame publication interval");
    machine.advance_to(700000);
    check(machine.inspect_memory(0)==2 && machine.inspect_memory(0x10)==0x3c,"next NMI/controller snapshot");
    check(std::all_of(machine.frame().samples.begin(),machine.frame().samples.end(),[](auto sample) { return sample==0x21; }),
          "CPU configured CHR/palette/scroll did not produce expected solid frame");
    std::vector<std::uint8_t> buttons;
    for (const auto& event:machine.trace()) if (event.kind==TraceKind::cpu_read && event.address==0x4016) {
        check((event.data&0xe0)==0x40 && !(event.data&0x1e),"controller driven bits/open-bus high bits");
        buttons.push_back(static_cast<std::uint8_t>(event.data&1));
    }
    check(buttons.size()==16,"controller capture count");
    for (unsigned i=0;i<16;++i) check(buttons[i]==(((i<8 ? 0xa5 : 0x3c)>>(i%8))&1),"recorded controller timeline");
    const auto states=observation_hash(machine.state(),ram(machine)), events=event_hash(machine.trace()), frame=frame_hash(machine.frame());
    Machine replay(video_input_program(),Profile{.trace=true});
    replay.schedule_input(1,0x81); replay.schedule_input(330000,0xa5); replay.schedule_input(330800,0x3c);
    for (rse::Tick target=997;target<700000;target+=997) replay.advance_to(target);
    replay.advance_to(700000);
    check(observation_hash(replay.state(),ram(replay))==states && event_hash(replay.trace())==events && frame_hash(replay.frame())==frame,
          "video/input state, event, and frame checksums depend on chunking");
}
rse::nes::Cartridge dma_program() {
    NesProgram p;
    p.emit({0x78,0xa2,0xff,0x9a,0xa2,0}); p.label("fill");
    p.emit({0x8a,0x9d,0,2,0xe8}); p.branch(0xd0,"fill");
    p.emit({0xa9,0xfc,0x8d,3,0x20,0xa9,2,0x8d,0x14,0x40,0xa2,0});
    p.label("readback"); p.emit({0x8e,3,0x20,0xad,4,0x20,0x9d,0,3,0xe8}); p.branch(0xd0,"readback");
    p.emit({0xa9,0xaa,0x85,0}); p.label("idle"); p.jump(0x4c,"idle");
    return p.cartridge();
}
void dma_transfer() {
    for (bool get_odd:{false,true}) {
        Machine machine(dma_program(),Profile{.dma_get_on_odd=get_odd,.trace=true});
        machine.advance_to(120000);
        check(machine.inspect_memory(0)==0xaa,"DMA fixture did not finish");
        unsigned gets=0,puts=0,halts=0,aligns=0;
        std::uint64_t first=0,last=0;
        for (const auto& event:machine.trace()) {
            if (event.kind==TraceKind::dma_halt) { ++halts; first=event.cycle; }
            if (event.kind==TraceKind::dma_align) ++aligns;
            if (event.kind==TraceKind::dma_get) {
                check(event.address==0x200+gets && event.data==gets && bool(event.cycle&1)==get_odd,"DMA mapped source/get phase"); ++gets;
            }
            if (event.kind==TraceKind::dma_put) {
                check(event.address==0x2004 && event.data==puts && bool(event.cycle&1)!=get_odd,"DMA mapped OAM/put phase");
                ++puts; last=event.cycle;
            }
        }
        check(first==3099 && last==(get_odd?3612:3611) && halts==1 && aligns==unsigned(get_odd) && gets==256 && puts==256,
              "DMA 513/514 timing reference");
        for (const auto& event:machine.trace()) {
            if (event.cycle>=first && event.cycle<=last)
                check(event.kind!=TraceKind::cpu_read && event.kind!=TraceKind::cpu_write,"CPU executed while DMA owned bus");
        }
        for (unsigned i=0;i<256;++i) {
            auto value=static_cast<std::uint8_t>((i+4)&255);
            if ((i&3)==2) value&=0xe3;
            check(machine.inspect_memory(static_cast<std::uint16_t>(0x300+i))==value,"DMA OAM wrap/attribute bits");
        }
        check(machine.state().ppu.dots==30000 && machine.state().cpu_cycles==10000,"PPU/CPU oscillator stopped during DMA");
    }
    NesProgram p; p.emit({0xee,0x14,0x40}); p.label("idle"); p.jump(0x4c,"idle");
    Machine rmw(p.cartridge(),Profile{.trace=true}); rmw.advance_to(10000);
    std::vector<TraceEvent> requests;
    for (auto event:rmw.trace()) if (event.kind==TraceKind::cpu_write && event.address==0x4014) requests.push_back(event);
    check(requests.size()==2 && requests[0].data==0x40 && requests[0].cycle==12 && requests[1].data==0x41 && requests[1].cycle==13,
          "RMW consecutive DMA writes/open bus");
    check(rmw.state().dma.page==0x41,"last RMW page must win before halt");
    for (const auto& event:rmw.trace()) if (event.kind==TraceKind::dma_get)
        check((event.address>>8)==0x41,"DMA used first rather than last requested page");
}
void interrupts() {
    {
        NesProgram p; p.emit({0x58,0xea,0x4c,1,0x80}); p.at(0x9100); p.emit({0xe6,1,0x40});
        Machine machine(p.cartridge(),Profile{.trace=true});
        machine.schedule_irq(120,true); machine.schedule_irq(280,false); machine.advance_to(400);
        check(machine.inspect_memory(1)==1,"IRQ handler or deassertion");
        for (auto [address,tick]:{std::pair{0xfffe,204},std::pair{0xffff,216}})
            check(std::any_of(machine.trace().begin(),machine.trace().end(),[&](auto e) { return e.kind==TraceKind::cpu_read && e.address==address && e.tick==tick; }),"IRQ vector/timestamp reference");
    }
    for (bool dma : {false,true}) {
        NesProgram p; p.emit({0xa9,0x80,0x8d,0,0x20,0x58}); p.label("idle"); p.jump(0x4c,"idle");
        p.at(0x9000); p.emit({0xe6,0,0x40}); p.at(0x9100);
        if (dma) p.emit({0xa9,2,0x8d,0x14,0x40,0xe6,2,0x40}); else p.emit({0xe6,1,0x40});
        Machine machine(p.cartridge(),Profile{.trace=true});
        machine.schedule_irq(dma?326000:330096,true); machine.schedule_irq(dma?326100:330496,false);
        if (dma) {
            machine.advance_to(330096);
            check(machine.state().dma.active && machine.state().cpu.pc==0x9105 && machine.state().nmi,
                  "NMI must assert while DMA holds the pending opcode fetch");
        }
        machine.advance_to(340000);
        check(machine.inspect_memory(0)==1 && machine.inspect_memory(dma?2:1)==1,"NMI/IRQ handler execution ordering");
        const auto nmi_tick=dma?332460:330192;
        check(std::any_of(machine.trace().begin(),machine.trace().end(),[&](auto e) { return e.kind==TraceKind::cpu_read && e.address==0xfffa && e.tick==nmi_tick; }),"NMI delivery/timestamp reference");
        if (dma) {
            check(std::any_of(machine.trace().begin(),machine.trace().end(),[](auto e) { return e.kind==TraceKind::dma_halt && e.tick==326184; }),"IRQ-triggered DMA halt time");
            check(std::any_of(machine.trace().begin(),machine.trace().end(),[](auto e) { return e.kind==TraceKind::dma_put && e.tick==332328; }),"IRQ-triggered DMA completion time");
        } else {
            check(std::any_of(machine.trace().begin(),machine.trace().end(),[](auto e) { return e.kind==TraceKind::cpu_read && e.address==0xfffe && e.tick==330408; }),"NMI must precede simultaneous IRQ");
        }
    }
}
}
int main(int argc,char** argv) {
    try {
        if (argc==2 && std::string(argv[1])=="--print-goldens") {
            std::cout<<std::hex<<"events "<<event_hash(loop_oracle())<<"\nstate "<<loop_state_oracle()
                     <<"\nblank-frame "<<frame_hash(rse::video::PpuFrame{})<<'\n'; return 0;
        }
        deterministic_loop(); clocks_and_failures(); memory_decode(); register_phase_order(); video_and_input(); dma_transfer(); interrupts();
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
    return 0;
}
