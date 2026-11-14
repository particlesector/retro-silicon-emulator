// SPDX-License-Identifier: Apache-2.0
#include <rse/audio/rp2a03_apu.hpp>
#include <rse/audio/pcm.hpp>
#include <rse/devices/rp2a03_dma.hpp>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace rse::audio;
void check(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
void to(Rp2a03Apu& a,std::uint64_t end) { while (a.state().cycles<end) a.clock((a.state().cycles&1)==0); }
void frame_sequence() {
    Rp2a03Apu a;
    to(a,7456); check(a.state().quarters==0,"early quarter");
    to(a,7457); check(a.state().quarters==1 && a.state().halves==0,"quarter 7457");
    to(a,14913);check(a.state().quarters==2 && a.state().halves==1,"half 14913");
    to(a,22371);check(a.state().quarters==3 && a.state().halves==1,"quarter 22371");
    to(a,29827);check(!a.irq_asserted(),"early frame IRQ");
    for (unsigned i=29828;i<=29830;++i) {
        to(a,i);check(a.read_status()==0x40 && !a.irq_asserted(),"frame IRQ reassert/read clear");
    }
    check(a.state().quarters==4 && a.state().halves==2 && a.state().frame_cycle==0,"four-step wrap");
    for (unsigned parity:{0,1}) {
        Rp2a03Apu b;to(b,parity+1);const auto start=b.state().cycles;
        b.write(23,0xc0);
        const auto delay=parity==0 ? 3 : 4;
        to(b,start+delay-1);check(b.state().quarters==0,"early delayed reset");
        to(b,start+delay);check(b.state().quarters==1 && b.state().halves==1 && b.state().five_step,"five-step reset clocks");
        to(b,start+delay+37280);check(b.state().quarters==4 && b.state().halves==2 && !b.irq_asserted(),"five-step no fourth clock/IRQ");
        to(b,start+delay+37281);check(b.state().quarters==5 && b.state().halves==3,"five-step last clock");
    }
    to(a,59658);check(a.irq_asserted(),"next frame IRQ");a.write(23,0x40);check(!a.irq_asserted(),"inhibit clears immediately");
}
void envelopes_lengths() {
    Envelope e{.control=1,.start=true};e.clock();check(e.decay==15 && e.divider==1,"envelope start");
    e.clock();check(e.decay==15,"envelope divider");e.clock();check(e.decay==14,"envelope decay");
    for (unsigned i=0;i<28;++i) e.clock();check(e.decay==0,"envelope zero");
    e.control=0x21;e.clock();e.clock();check(e.decay==15,"envelope loop");
    e.control=0x17;check(e.output()==7,"constant volume override");
    constexpr unsigned table[]{10,254,20,2,40,4,80,6,160,8,60,10,14,12,26,14,12,16,24,18,48,20,96,22,192,24,72,26,16,28,32,30};
    for (unsigned i=0;i<32;++i) {
        Rp2a03Apu a;a.write(3,static_cast<std::uint8_t>(i<<3));check(a.state().pulse[0].length==0,"disabled length load");
        a.write(21,15); for (auto r:{3,7,11,15}) a.write(r,static_cast<std::uint8_t>(i<<3));
        check(a.read_status()==15,"length status");to(a,14913);
        check(a.state().pulse[0].length==table[i]-1 && a.state().triangle.length==table[i]-1 && a.state().noise.length==table[i]-1,"length table/half clock");
        a.write(21,0);check(a.read_status()==0,"disable clears length");a.write(21,15);check(a.read_status()==0,"enable does not reload");
    }
}
void channels() {
    constexpr unsigned duties[]{0x02,0x06,0x1e,0xf9};
    for (unsigned duty=0;duty<4;++duty) {
        Rp2a03Apu a;a.write(21,1);a.write(0,static_cast<std::uint8_t>((duty<<6)|0x3f));a.write(2,8);a.write(3,0);
        for (unsigned phase=1;phase<=8;++phase) {
            to(a,1+(phase-1)*18);
            check(a.output().pulse1==((duties[duty]&(1<<(phase&7)))?15:0),"pulse duty/timer");
        }
        a.write(2,7);check(a.output().pulse1==0,"pulse ultrasonic mute");
    }
    Rp2a03Apu sweep;sweep.write(21,3);
    for (auto base:{0,4}) { sweep.write(base,0x3f);sweep.write(base+2,0x00);sweep.write(base+3,1);sweep.write(base+1,0x89); }
    to(sweep,14913);
    check(sweep.state().pulse[0].period==127 && sweep.state().pulse[1].period==128,"sweep negate channel difference");
    Rp2a03Apu overflow;overflow.write(21,1);overflow.write(0,0xff);overflow.write(2,0xff);overflow.write(3,7);
    to(overflow,1);check(overflow.output().pulse1==0,"disabled sweep still mutes overflow");
    Rp2a03Apu t;t.write(21,4);t.write(8,2);t.write(10,1);t.write(11,0);
    to(t,7456);check(t.state().triangle.linear==0 && t.output().triangle==0,"triangle waits for linear reload");
    to(t,7457);check(t.state().triangle.linear==2 && !t.state().triangle.reload,"linear reload flag");
    const auto phase=t.state().triangle.phase;to(t,7461);check(t.state().triangle.phase==((phase+2)&31),"triangle CPU-rate timer");
    to(t,14913);check(t.state().triangle.linear==1,"linear decrement");
    to(t,22371);check(t.state().triangle.linear==0,"linear expired");
    const auto held=t.output().triangle;to(t,23000);check(t.output().triangle==held,"triangle holds DAC");
    t.write(8,0x83);t.write(11,0);to(t,29829);check(t.state().triangle.linear==3 && t.state().triangle.reload,"linear control holds reload");
    for (bool mode:{false,true}) {
        Rp2a03Apu n;n.write(21,8);n.write(12,0x3f);n.write(14,mode?0x80:0);n.write(15,0);
        unsigned shift=1;
        for (unsigned i=0;i<1000;++i) {
            shift=(shift>>1)|(((shift^(shift>>(mode?6:1)))&1)<<14);
            to(n,1+4*i);check(n.state().noise.shift==shift && n.output().noise==((shift&1)?0:15),"noise tap/timer/output");
        }
    }
}
void dmc_reader() {
    Rp2a03Apu a;a.write(16,0x8f);a.write(17,64);a.write(18,0xff);a.write(19,4);a.write(21,16);
    to(a,2);check(!a.dma_request(),"early DMC load");to(a,3);check(a.dma_request()==0xffc0,"DMC load request/address");
    a.accept_dma(0x55);check(a.state().dmc.remaining==64 && a.state().dmc.buffer_full,"DMC accept");
    unsigned fetched=1;
    while (fetched<65) {
        a.clock((a.state().cycles&1)==0);
        if (a.dma_request()) {
            check(*a.dma_request()==(fetched==64 ? 0x8000 : 0xffc0+fetched),"DMC address wrap");
            a.accept_dma(0x55);++fetched;
        }
    }
    check(a.irq_asserted() && a.read_status()==0x80 && a.irq_asserted(),"DMC terminal IRQ/status does not clear");
    a.write(21,0);check(!a.irq_asserted() && a.state().dmc.buffer_full,"disable preserves buffered sample");
    a.write(17,127);a.write(16,0x4f);a.write(19,0);a.write(21,16);
    while (!a.dma_request()) a.clock((a.state().cycles&1)==0);
    a.accept_dma(0xff);check(a.state().dmc.remaining==1 && !a.irq_asserted(),"DMC loop restart");
    to(a,a.state().cycles+1000);check(a.output().dmc<=127,"DMC saturation");
    Rp2a03Apu bits;bits.write(16,15);bits.write(17,64);bits.write(21,16);to(bits,3);bits.accept_dma(0x05);
    // Initial 428-cycle timer, then seven silent 54-cycle ticks load the byte.
    to(bits,806);check(!bits.state().dmc.silence && bits.state().dmc.shift==5,"DMC buffer transfer boundary");
    for (auto [time,value]:{std::pair{860,66},std::pair{914,64},std::pair{968,66},std::pair{1022,64}}) {
        to(bits,time);check(bits.output().dmc==value,"DMC LSB-first deltas/rate");
    }
    bits.write(16,0);check(!bits.irq_asserted(),"DMC IRQ disable");
}
void period_tables_and_gates() {
    constexpr unsigned noise[]{4,8,16,32,64,96,128,160,202,254,380,508,762,1016,2034,4068};
    constexpr unsigned dmc[]{428,380,340,320,286,254,226,214,190,160,142,128,106,84,72,54};
    for (unsigned rate=0;rate<16;++rate) {
        Rp2a03Apu a;a.write(14,static_cast<std::uint8_t>(rate));a.write(16,static_cast<std::uint8_t>(rate));
        to(a,1);check(a.state().noise.timer==noise[rate]-1,"noise period table reload");
        const auto shift=a.state().noise.shift;to(a,noise[rate]);check(a.state().noise.shift==shift,"noise period early clock");
        to(a,noise[rate]+1);check(a.state().noise.shift!=shift,"noise period missing clock");
        Rp2a03Apu b;b.write(16,static_cast<std::uint8_t>(rate));to(b,428);
        check(b.state().dmc.timer==dmc[rate]-1 && b.state().dmc.bits==7,"DMC period table reload");
        to(b,428+dmc[rate]-1);check(b.state().dmc.bits==7,"early DMC timer");
        to(b,428+dmc[rate]);check(b.state().dmc.bits==6,"DMC timer interval");
    }
    Rp2a03Apu halted;halted.write(21,15);
    for (auto base:{0,4,12}) {halted.write(base,0x20);halted.write(base+3,0);}
    halted.write(8,0x80);halted.write(11,0);to(halted,29829);
    check(halted.state().pulse[0].length==10 && halted.state().pulse[1].length==10 &&
          halted.state().noise.length==10 && halted.state().triangle.length==10,"length halt flags");
    Rp2a03Apu stopped;stopped.write(21,16);stopped.write(21,0);to(stopped,100);
    check(!stopped.dma_request() && stopped.read_status()==0,"disable cancels unhalted DMC request");
    try {stopped.accept_dma(0);throw std::runtime_error("unexpected DMC accept allowed");}catch(const std::logic_error&) {}
    Rp2a03Apu upper;upper.write(16,15);upper.write(17,126);upper.write(21,16);to(upper,3);upper.accept_dma(0xff);
    to(upper,1238);check(upper.output().dmc==126,"upper DMC DAC saturation");
    Rp2a03Apu lower;lower.write(16,15);lower.write(17,1);lower.write(21,16);to(lower,3);lower.accept_dma(0);
    to(lower,1238);check(lower.output().dmc==1,"lower DMC DAC saturation");
}
void arbitration() {
    using namespace rse::devices;
    for (bool start_get:{false,true}) {
        Rp2a03Dma dma;unsigned held=0;
        for (unsigned i=0;i<4;++i) {
            const auto a=dma.clock(false,0x4016,(i%2==0)?start_get:!start_get,0xc000);
            if (a.dmc!=DmcKind::none) ++held;
            if (a.dmc==DmcKind::get) {check(a.address==0xc000,"DMC source");break;}
        }
        check(held==(start_get?3:4),"DMC 3/4 slot hold");
    }
    Rp2a03Dma delayed;check(delayed.clock(true,0x4015,true,0xc000).dmc==DmcKind::none,"DMC must wait for CPU write");
    for (unsigned collision=0;collision<514;++collision) {
        Rp2a03Dma dma;dma.request_oam(2);unsigned gets=0,puts=0,dmc=0;bool requested=false;
        for (unsigned slot=0;slot<530;++slot) {
            if (slot==collision) requested=true;
            const auto a=dma.clock(false,0x8000,(slot&1)==0,requested?std::optional<std::uint16_t>(0xc000):std::nullopt);
            if (a.dmc==DmcKind::get) {requested=false;++dmc;}
            if (a.oam.kind==DmaKind::get) {check(a.oam.address==0x200+gets,"OAM source lost under contention");dma.accept_oam(static_cast<std::uint8_t>(gets++));}
            if (a.oam.kind==DmaKind::put) check(a.oam.data==static_cast<std::uint8_t>(puts++),"OAM data lost under contention");
        }
        check(gets==256 && puts==256 && dmc==1,"OAM/DMC overlap completion");
    }
}
void pcm() {
    check(NtscPcm::mix({})==0,"zero mixer");
    const auto max=NtscPcm::mix({15,15,15,15,127});check(max>32000 && max<=32767,"nonlinear mixer bounds");
    check(NtscPcm::mix({0,0,15,15,127})-NtscPcm::mix({0,0,0,0,127})<NtscPcm::mix({0,0,15,15,0}),"DMC reduces TND gain");
    NtscPcm converter;unsigned count=0;std::int16_t last=0;
    for (unsigned i=0;i<1'968'750;++i) if (auto sample=converter.clock({0,0,0,0,64})) {++count;last=*sample;}
    check(count==52800 && last==0,"exact rational sample count/DC decay");
}
}
int main() {
    try {frame_sequence();envelopes_lengths();channels();dmc_reader();period_tables_and_gates();arbitration();pcm();}
    catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
