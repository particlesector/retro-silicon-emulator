// SPDX-License-Identifier: Apache-2.0
#include <rse/systems/nes/machine.hpp>
#include <rse/systems/nes/bus.hpp>
#include <rse/audio/pcm.hpp>
#include <rse/devices/rp2a03_dma.hpp>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace rse::nes {
struct Machine::Impl {
    struct CpuConnection final : CpuBus {
        Impl& system;
        explicit CpuConnection(Impl& owner) : system(owner) {}
        bool acquire(std::uint16_t address, bool writing, std::uint64_t cycle) override {
            const bool get = bool(cycle & 1) == system.profile.dma_get_on_odd;
            const auto arbitration = system.dma.clock(writing,address,get,system.apu.dma_request());
            using devices::DmcKind;
            if (arbitration.dmc==DmcKind::get) {
                const auto value=system.read_memory(arbitration.address);
                system.apu.accept_dma(value);
                system.record(TraceKind::dmc_get,arbitration.address,value,cycle);
                system.route_irq(); return false;
            }
            const auto action=arbitration.oam;
            using devices::DmaKind;
            if (action.kind == DmaKind::cpu) {
                if (arbitration.dmc==DmcKind::none) return true;
                const auto value=system.read_memory(arbitration.address);
                const auto kind=arbitration.dmc==DmcKind::halt ? TraceKind::dmc_halt :
                    arbitration.dmc==DmcKind::dummy ? TraceKind::dmc_dummy : TraceKind::dmc_align;
                system.record(kind,arbitration.address,value,cycle);
                system.route_nmi(); system.route_irq(); return false;
            }
            std::uint8_t value = action.data;
            TraceKind kind;
            switch (action.kind) {
            case DmaKind::halt: kind=TraceKind::dma_halt; value=system.read_memory(action.address); break;
            case DmaKind::align: kind=TraceKind::dma_align; value=system.read_memory(action.address); break;
            case DmaKind::get:
                kind=TraceKind::dma_get; value=system.read_memory(action.address); system.dma.accept_oam(value); break;
            case DmaKind::put: kind=TraceKind::dma_put; system.write_memory(action.address,value); break;
            default: throw std::logic_error("invalid DMA action");
            }
            system.record(kind,action.address,value,cycle);
            system.route_nmi();
            system.route_irq();
            return false;
        }
        std::uint8_t read(std::uint16_t address, std::uint64_t cycle) override {
            const auto value=system.read_memory(address);
            system.record(TraceKind::cpu_read,address,value,cycle);
            system.route_nmi();
            system.route_irq();
            return value;
        }
        void write(std::uint16_t address, std::uint8_t value, std::uint64_t cycle) override {
            system.write_memory(address,value);
            system.record(TraceKind::cpu_write,address,value,cycle);
            system.route_nmi();
            system.route_irq();
        }
    };
    struct PpuConnection final : video::PpuBus {
        Impl& system;
        explicit PpuConnection(Impl& owner) : system(owner) {}
        std::uint8_t read(std::uint16_t address, std::uint64_t dot) override {
            const auto value=system.memory.ppu_read(address);
            system.record(TraceKind::ppu_read,address,value,dot);
            return value;
        }
        void write(std::uint16_t address, std::uint8_t value, std::uint64_t dot) override {
            system.memory.ppu_write(address,value);
            system.record(TraceKind::ppu_write,address,value,dot);
        }
    };
    enum class ScheduledKind { input, irq };
    struct Scheduled { Tick tick; ScheduledKind kind; std::uint8_t data; };
    Profile profile;
    Bus memory;
    devices::NesController controller;
    devices::Rp2a03Dma dma;
    audio::Rp2a03Apu apu;
    audio::NtscPcm pcm;
    std::vector<std::int16_t> samples;
    CpuConnection cpu_bus;
    PpuConnection ppu_bus;
    Cpu cpu;
    video::Rp2c02 ppu;
    Tick tick{}, active_tick{};
    std::optional<Tick> next_ppu, next_cpu;
    std::uint8_t latch{};
    bool irq{}, nmi{}, apu_irq{}, faulted{};
    std::vector<Scheduled> scheduled;
    std::size_t next_event{};
    std::vector<TraceEvent> events;

    Impl(Cartridge cartridge, Profile configuration)
        : profile(configuration), memory(std::move(cartridge)), cpu_bus(*this), ppu_bus(*this),
          cpu(cpu_bus), ppu(ppu_bus) {
        if (profile.ppu_phase>=4 || profile.cpu_phase>=12 || (profile.trace && !profile.trace_limit) ||
            (profile.capture_audio && !profile.audio_limit))
            throw std::invalid_argument("invalid NES clock/trace profile");
        next_ppu=profile.ppu_phase ? profile.ppu_phase : 4;
        next_cpu=profile.cpu_phase ? profile.cpu_phase : 12;
    }
    void healthy() const {
        if (faulted) throw std::logic_error("NES machine requires reset after failure");
    }
    void record(TraceKind kind, std::uint16_t address, std::uint64_t data, std::uint64_t cycle=0) {
        if (!profile.trace) return;
        if (events.size()==profile.trace_limit) throw std::length_error("NES trace limit exceeded");
        events.push_back({active_tick,kind,address,data,cycle});
    }
    void route_nmi() {
        const bool level=ppu.nmi_asserted();
        if (level!=nmi) { nmi=level; record(TraceKind::nmi,0,level); }
        cpu.set_nmi(level);
    }
    void route_irq() {
        const auto level=apu.irq_asserted();
        if (level!=apu_irq) { apu_irq=level; record(TraceKind::apu_irq,0,level); }
        cpu.set_irq(irq || apu_irq);
    }
    std::uint8_t read_memory(std::uint16_t address) {
        std::uint8_t value=latch;
        if (address<0x2000 || address>=0x8000) value=memory.cpu_read(address);
        else if (address<0x4000) value=ppu.cpu_read(address & 7);
        else if (address==0x4015) value=(latch&0x20)|apu.read_status();
        else if (address==0x4016) value=(latch & 0xe0) | std::uint8_t(!controller.read());
        else if (address==0x4017) value=latch & 0xe0;
        latch=value;
        return value;
    }
    void write_memory(std::uint16_t address, std::uint8_t value) {
        latch=value;
        if (address<0x2000) memory.cpu_write(address,value);
        else if (address<0x4000) ppu.cpu_write(address & 7,value);
        else if (address==0x4014) dma.request_oam(value);
        else if (address==0x4016) controller.strobe(value & 1);
        else if (address<=0x4013 || address==0x4015 || address==0x4017) apu.write(address&0x1f,value);
    }
    void schedule(Tick when, ScheduledKind kind, std::uint8_t data) {
        healthy();
        if (when<=tick) throw std::invalid_argument("NES events must be in the future");
        if (next_event && next_event>=scheduled.size()/2) {
            scheduled.erase(scheduled.begin(),scheduled.begin()+next_event);
            next_event=0;
        }
        const auto position=std::upper_bound(scheduled.begin()+next_event,scheduled.end(),when,
            [](Tick time,const Scheduled& event) { return time<event.tick; });
        scheduled.insert(position,{when,kind,data});
    }
    static void increment(std::optional<Tick>& edge, Tick period) {
        if (std::numeric_limits<Tick>::max()-*edge<period) edge.reset();
        else *edge+=period;
    }
    void advance_to(Tick target) {
        healthy();
        if (target<tick) throw std::invalid_argument("cannot rewind NES time");
        try {
            while (tick<target) {
                Tick next=target;
                if (next_ppu) next=std::min(next,*next_ppu);
                if (next_cpu) next=std::min(next,*next_cpu);
                if (next_event<scheduled.size()) next=std::min(next,scheduled[next_event].tick);
                active_tick=next;
                while (next_event<scheduled.size() && scheduled[next_event].tick==next) {
                    const auto event=scheduled[next_event++];
                    if (event.kind==ScheduledKind::input) {
                        controller.set_buttons(event.data);
                        record(TraceKind::input,0,event.data);
                    } else {
                        irq=event.data!=0; route_irq();
                        record(TraceKind::irq,0,irq);
                    }
                }
                if (next_ppu && *next_ppu==next) {
                    const auto frames=ppu.frame().number;
                    ppu.clock();
                    increment(next_ppu,4);
                    route_nmi();
                    if (ppu.frame().number!=frames) record(TraceKind::frame,0,ppu.frame().number);
                }
                if (next_cpu && *next_cpu==next) {
                    const auto cycle=cpu.cycles()+1;
                    apu.clock(bool(cycle&1)==profile.dma_get_on_odd);
                    route_irq();
                    cpu.clock();
                    if (profile.capture_audio) {
                        if (const auto sample=pcm.clock(apu.output())) {
                            if (samples.size()==profile.audio_limit) throw std::length_error("NES audio queue limit exceeded");
                            samples.push_back(*sample);
                        }
                    }
                    increment(next_cpu,12);
                }
                tick=next;
            }
        } catch (...) { faulted=true; throw; }
    }
};

Machine::Machine(Cartridge cartridge, Profile profile)
    : initial_(std::move(cartridge)), profile_(profile), impl_(std::make_unique<Impl>(initial_,profile)) {}
Machine::~Machine() = default;
void Machine::advance_to(Tick target) { impl_->advance_to(target); }
const video::PpuFrame& Machine::run_frame() {
    impl_->healthy();
    const auto current=impl_->ppu.frame().number;
    while (impl_->ppu.frame().number==current) {
        if (!impl_->next_ppu) throw std::overflow_error("NES clock exhausted");
        impl_->advance_to(*impl_->next_ppu);
    }
    return impl_->ppu.frame();
}
void Machine::reset() { impl_=std::make_unique<Impl>(initial_,profile_); }
void Machine::schedule_input(Tick tick,std::uint8_t buttons) { impl_->schedule(tick,Impl::ScheduledKind::input,buttons); }
void Machine::schedule_irq(Tick tick,bool asserted) { impl_->schedule(tick,Impl::ScheduledKind::irq,asserted); }
MachineState Machine::state() const noexcept {
    return {impl_->tick,impl_->cpu.state(),impl_->ppu.state(),impl_->cpu.cycles(),impl_->latch,
        impl_->controller.buttons(),impl_->controller.state(),impl_->dma.oam_state(),
        impl_->cpu.at_instruction_boundary(),impl_->irq,impl_->nmi,impl_->faulted};
}
const video::PpuFrame& Machine::frame() const noexcept { return impl_->ppu.frame(); }
std::optional<std::uint8_t> Machine::inspect_memory(std::uint16_t address) const noexcept {
    if (address<0x2000 || address>=0x8000) return impl_->memory.cpu_read(address);
    return std::nullopt;
}
std::span<const TraceEvent> Machine::trace() const noexcept { return impl_->events; }
audio::ApuState Machine::apu_state() const noexcept { return impl_->apu.state(); }
std::vector<std::int16_t> Machine::take_audio() { std::vector<std::int16_t> result; result.swap(impl_->samples); return result; }
} // namespace rse::nes
