// SPDX-License-Identifier: Apache-2.0
#include "cpu_test_bus.hpp"
#include <iostream>

struct ArbitratedBus final : rse::cpu::Bus {
    TestBus memory;
    std::function<bool(std::uint16_t,bool,std::uint64_t)> grant;
    bool acquire(std::uint16_t a,bool w,std::uint64_t c) override { return !grant || grant(a,w,c); }
    std::uint8_t read(std::uint16_t a,std::uint64_t c) override { return memory.read(a,c); }
    void write(std::uint16_t a,std::uint8_t v,std::uint64_t c) override { memory.write(a,v,c); }
};
int main() {
    using rse::cpu::Mos6502;
    using rse::cpu::CpuState;
    try {
        ArbitratedBus bus;
        bus.memory.ram[0x8000]=0xa9; bus.memory.ram[0x8001]=0x42;
        Mos6502 cpu(bus,CpuState{0x8000,0xfd,0,0,0,0x24});
        bus.grant=[](auto,bool,std::uint64_t cycle) { return cycle>4; };
        for (unsigned i=0;i<4;++i) cpu.clock();
        check(cpu.state().pc==0x8000 && cpu.state().a==0 && cpu.cycles()==4 && bus.memory.trace.empty(),"held opcode read changed state or accessed CPU bus");
        cpu.clock();
        check(cpu.state().pc==0x8001,"PC must increment only after granted fetch");
        bus.grant=[](auto,bool,std::uint64_t cycle) { return cycle>8; };
        for (unsigned i=0;i<3;++i) cpu.clock();
        check(cpu.state().pc==0x8001 && cpu.state().a==0,"held operand read was consumed");
        cpu.clock();
        check(cpu.state().pc==0x8002 && cpu.state().a==0x42 && cpu.at_instruction_boundary(),"held instruction did not resume");
        check(bus.memory.trace==std::vector<Access>{{0x8000,0xa9,false,5},{0x8001,0x42,false,9}},"granted access timestamps");

        ArbitratedBus pins;
        pins.memory.ram.fill(0xea); pins.memory.ram[0xfffa]=0; pins.memory.ram[0xfffb]=0x90;
        Mos6502 interrupt(pins,CpuState{0x8000,0xfd,0,0,0,0x24});
        pins.grant=[&](auto,bool,std::uint64_t cycle) {
            interrupt.set_nmi(cycle==2);
            return cycle>10;
        };
        for (unsigned i=0;i<12;++i) interrupt.clock();
        check(interrupt.at_instruction_boundary() && interrupt.state().pc==0x8001,"held NOP did not complete");
        step(interrupt);
        check(interrupt.state().pc==0x9000 && interrupt.cycles()==19,"NMI pulse was lost while CPU was held");

        ArbitratedBus writes;
        writes.memory.ram[0x8000]=0x85; writes.memory.ram[0x8001]=0x10;
        Mos6502 invalid(writes,CpuState{0x8000,0xfd,0x55,0,0,0x24});
        writes.grant=[](auto,bool writing,auto) { return !writing; };
        invalid.clock(); invalid.clock();
        bool rejected=false;
        try { invalid.clock(); } catch(const std::logic_error&) { rejected=true; }
        check(rejected && writes.memory.ram[0x10]==0,"write halts must fail without committing a write");
        writes.grant={}; invalid.reset(); step(invalid);
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
    return 0;
}
