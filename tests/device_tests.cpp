// SPDX-License-Identifier: Apache-2.0
#include <rse/chips/cd4021.hpp>
#include <rse/devices/nes_controller.hpp>
#include <rse/devices/rp2a03_oam_dma.hpp>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
void shift_register() {
    rse::chips::Cd4021 chip;
    chip.set_parallel(0xa6); chip.set_load(true);
    check(chip.output(),"parallel load must be asynchronous");
    chip.set_parallel(0x36); check(!chip.output(),"high load must track input pins");
    chip.clock(); check(!chip.output(),"clock must not shift while load is high");
    chip.set_load(false); chip.set_parallel(0xff);
    for (unsigned i=0;i<8;++i) {
        check(chip.output()==bool(0x36 & (0x80>>i)),"4021 serial ordering"); chip.clock();
    }
    check(!chip.output(),"low serial input must shift in zeros");
    chip.set_serial(true);
    for (unsigned i=0;i<8;++i) chip.clock();
    check(chip.output(),"serial input must reach Q8 after eight clocks");
}
void controller() {
    for (unsigned buttons=0;buttons<256;++buttons) {
        rse::devices::NesController pad;
        pad.set_buttons(static_cast<std::uint8_t>(buttons));
        pad.strobe(true);
        for (unsigned i=0;i<3;++i) check(!pad.read()==(buttons&1),"held strobe must repeatedly report live A");
        pad.strobe(false);
        pad.set_buttons(static_cast<std::uint8_t>(buttons^0xff));
        for (unsigned i=0;i<8;++i) check(!pad.read()==((buttons>>i)&1),"controller serial button order/capture");
        for (unsigned i=0;i<8;++i) check(!pad.read()==1,"controller tail must be one");
    }
    rse::devices::NesController pad;
    pad.strobe(true); pad.set_buttons(1); check(!pad.read()==1,"live A while strobe high");
    pad.set_buttons(0); check(!pad.read()==0,"changed live A while strobe high");
    pad.set_buttons(0x81); pad.strobe(false);
    check(!pad.read()==1,"first A bit"); pad.strobe(false);
    check(!pad.read()==0,"writing low twice must not relatch");
}
void dma() {
    using namespace rse::devices;
    for (bool halt_get : {false,true}) {
        Rp2a03OamDma dma;
        dma.request(2);
        check(dma.clock(true,0x8000,true).kind==DmaKind::cpu,"DMA must not halt a write");
        dma.request(3);
        unsigned slots=0,gets=0,puts=0,aligns=0;
        for (;;) {
            const bool get=(slots%2==0) ? halt_get : !halt_get;
            const auto action=dma.clock(false,0x4016,get);
            ++slots;
            if (action.kind==DmaKind::halt || action.kind==DmaKind::align) {
                check(action.address==0x4016,"halt/alignment must repeat the held CPU address");
                aligns+=action.kind==DmaKind::align;
            } else if (action.kind==DmaKind::get) {
                check(get && action.address==0x300+gets,"DMA source sequence/last written page");
                dma.accept(static_cast<std::uint8_t>(gets++));
            } else if (action.kind==DmaKind::put) {
                check(!get && action.address==0x2004 && action.data==static_cast<std::uint8_t>(puts++),"DMA destination/data sequence");
            } else throw std::runtime_error("DMA unexpectedly released CPU");
            if (!dma.state().active) break;
            check(slots<=514,"DMA did not terminate");
        }
        check(slots==(halt_get?514:513) && aligns==unsigned(halt_get) && gets==256 && puts==256,"DMA slot counts");
        check(!dma.state().pending && dma.state().index==256,"DMA completion state");
        check(dma.clock(false,0x4016,true).kind==DmaKind::cpu,"DMA must release the held read");
    }
    // A halted side-effecting controller read is repeated on alignment/resume.
    for (bool halt_get : {false,true}) {
        NesController pad; pad.set_buttons(0x05); pad.strobe(true); pad.strobe(false);
        Rp2a03OamDma dma; dma.request(0);
        for (unsigned slot=0;dma.state().pending || dma.state().active;++slot) {
            const auto action=dma.clock(false,0x4016,(slot%2==0)?halt_get:!halt_get);
            if (action.kind==DmaKind::halt || action.kind==DmaKind::align) (void)pad.read();
            if (action.kind==DmaKind::get) dma.accept(0);
        }
        check(!pad.read()==(halt_get?1:0),"DMA idle reads must retain controller side effects");
    }
}
}
int main() {
    try { shift_register(); controller(); dma(); }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
    return 0;
}
