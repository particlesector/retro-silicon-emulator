// SPDX-License-Identifier: Apache-2.0
#include <rse/systems/nes/machine.hpp>
#include <rse/systems/nes/bus.hpp>
#include <rse/audio/pcm.hpp>
#include <rse/devices/rp2a03_dma.hpp>
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace rse::nes {
namespace {
using namespace session;
using enum ClockDomain;
using enum ValueKind;
using enum AddressSpace;
enum ValueIndex { cpu_bus, ppu_bus, ram, nametables, oam, pc, a, x, y, sp, p,
                  ppu_v, ppu_status, ppu_scanline, ppu_dot, apu_status, irq_line, nmi_line, dma_active };
constexpr std::array values{
    ValueInfo{"cpu.bus",memory,cpu,8,0x10000,mapped},
    ValueInfo{"ppu.bus",memory,video,8,0x4000,mapped},
    ValueInfo{"cpu.ram",memory,cpu,8,2048},
    ValueInfo{"ppu.nametable-ram",memory,video,8,2048},
    ValueInfo{"ppu.oam",memory,video,8,256},
    ValueInfo{"cpu.pc",register_value,cpu,16,1},
    ValueInfo{"cpu.a",register_value,cpu,8,1},
    ValueInfo{"cpu.x",register_value,cpu,8,1},
    ValueInfo{"cpu.y",register_value,cpu,8,1},
    ValueInfo{"cpu.sp",register_value,cpu,8,1},
    ValueInfo{"cpu.p",register_value,cpu,8,1},
    ValueInfo{"ppu.v",register_value,video,15,1},
    ValueInfo{"ppu.status",register_value,video,8,1},
    ValueInfo{"ppu.scanline",signal,video,9,1},
    ValueInfo{"ppu.dot",signal,video,9,1},
    ValueInfo{"apu.status",register_value,cpu,8,1},
    ValueInfo{"cpu.irq",signal,cpu,1,1},
    ValueInfo{"cpu.nmi",signal,cpu,1,1},
    ValueInfo{"dma.oam.active",signal,cpu,1,1}
};
// Indexed by the legacy TraceKind ordering. Expose names rather than ordinals.
constexpr std::array event_types{
    EventInfo{"cpu.read",cpu,"cpu.bus",8}, EventInfo{"cpu.write",cpu,"cpu.bus",8},
    EventInfo{"dma.oam.halt",cpu,"cpu.bus",8}, EventInfo{"dma.oam.align",cpu,"cpu.bus",8},
    EventInfo{"dma.oam.get",cpu,"cpu.bus",8}, EventInfo{"dma.oam.put",cpu,"cpu.bus",8},
    EventInfo{"ppu.read",video,"ppu.bus",8}, EventInfo{"ppu.write",video,"ppu.bus",8},
    EventInfo{"cpu.nmi.changed",master,{},1}, EventInfo{"external.irq",master,{},1},
    EventInfo{"controller.input",master,{},8}, EventInfo{"ppu.frame",video,{},64},
    EventInfo{"apu.irq.changed",master,{},1},
    EventInfo{"dma.dmc.halt",cpu,"cpu.bus",8}, EventInfo{"dma.dmc.dummy",cpu,"cpu.bus",8},
    EventInfo{"dma.dmc.align",cpu,"cpu.bus",8}, EventInfo{"dma.dmc.get",cpu,"cpu.bus",8}
};
static_assert(event_types.size() == static_cast<unsigned>(TraceKind::dmc_get)+1);
constexpr std::array boundaries{Boundary::master_tick,Boundary::cpu_cycle,Boundary::instruction,
    Boundary::scanline,Boundary::frame};

template<class Info>
std::size_t find_id(std::span<const Info> schema, std::string_view id) {
    const auto item=std::ranges::find(schema,id,&Info::id);
    if(item==schema.end()) throw std::invalid_argument("Unknown inspection ID");
    return item-schema.begin();
}
void validate_range(const ValueInfo& info, std::uint32_t first, std::size_t count) {
    if(first>info.extent || count>info.extent-first)
        throw std::out_of_range("Inspection range exceeds address domain");
}

struct Stream {
    Subscription options;
    std::vector<ObservationRecord> records;
    std::vector<std::optional<std::uint64_t>> previous;
    std::size_t delta_index{};
    std::uint64_t sequence{}, lost{};
    bool sampled{};
    void push(ObservationRecord record) noexcept {
        record.sequence=++sequence;
        if(records.size()==options.capacity) ++lost;
        else records.push_back(record);
    }
};
}
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
    std::unique_ptr<Stream> stream;
    bool recording;

    Impl(Cartridge cartridge, Profile configuration)
        : profile(configuration), memory(std::move(cartridge)), cpu_bus(*this), ppu_bus(*this),
          cpu(cpu_bus), ppu(ppu_bus), recording(profile.trace) {
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
        if (!recording) return;
        if (profile.trace) {
            if (events.size()==profile.trace_limit) throw std::length_error("NES trace limit exceeded");
            events.push_back({active_tick,kind,address,data,cycle});
        }
        if (!stream || !stream->options.events) return;
        const auto& event=event_types[static_cast<unsigned>(kind)];
        const auto& filter=stream->options;
        if (!filter.event_id.empty() && filter.event_id!=event.id) return;
        if (filter.addresses && (filter.addresses->id!=event.address_domain ||
            address<filter.addresses->first || address-filter.addresses->first>=filter.addresses->count)) return;
        const auto location=event.address_domain.empty() ? std::nullopt : std::optional<std::uint32_t>{address};
        if(kind==TraceKind::frame) cycle=data;
        else if(event.clock==ClockDomain::master) cycle=active_tick;
        stream->push({0,active_tick,RecordKind::event,Phase::after,event.id,location,data,{},cycle});
    }
    std::optional<std::uint64_t> inspect(std::size_t index, std::uint32_t address) const noexcept {
        const auto location=static_cast<std::uint16_t>(address);
        switch(index) {
        case ValueIndex::cpu_bus:
            if(location<0x2000 || location>=0x8000) return memory.cpu_read(location);
            if(location<0x4000) return ppu.cpu_peek(location & 7);
            if(location==0x4015) return (latch&0x20)|apu.peek_status();
            if(location==0x4016) return (latch&0xe0)|!controller.peek();
            if(location==0x4017) return latch&0xe0;
            return std::nullopt;
        case ValueIndex::ppu_bus:
            if(location>=0x3f00) return ppu.inspect_palette(location);
            return memory.ppu_read(location);
        case ram: return memory.cpu_read(location);
        case nametables: return memory.nametable_byte(location);
        case oam: return ppu.inspect_oam(static_cast<std::uint8_t>(address));
        case pc: return cpu.state().pc;
        case a: return cpu.state().a;
        case x: return cpu.state().x;
        case y: return cpu.state().y;
        case sp: return cpu.state().s;
        case p: return cpu.state().p;
        case ppu_v: return ppu.state().v;
        case ppu_status: return ppu.state().status;
        case ppu_scanline: return ppu.state().scanline;
        case ppu_dot: return ppu.state().dot;
        case apu_status: return apu.peek_status();
        case irq_line: return irq || apu_irq;
        case nmi_line: return nmi;
        case dma_active: return dma.oam_state().active;
        default: return std::nullopt;
        }
    }
    void sample(Phase phase, std::uint64_t index) noexcept {
        if (stream->options.phase!=phase) return;
        if (!stream->options.delta) {
            stream->push({0,active_tick,RecordKind::boundary,phase,{},{},{},{},index});
            return;
        }
        const auto& range=*stream->options.delta;
        for(std::uint32_t offset=0;offset<range.count;++offset) {
            const auto address=range.first+offset;
            const auto value=inspect(stream->delta_index,address);
            auto& old=stream->previous[offset];
            if(!stream->sampled || old!=value)
                stream->push({0,active_tick,RecordKind::delta,phase,range.id,
                    values[stream->delta_index].kind==ValueKind::memory ? std::optional{address} : std::nullopt,
                    value,old,index,!stream->sampled});
            old=value;
        }
        stream->sampled=true;
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
    template<bool sample_boundaries>
    void advance_to(Tick target) {
        healthy();
        if (target<tick) throw std::invalid_argument("cannot rewind NES time");
        const auto boundary=sample_boundaries ? stream->options.boundary : std::nullopt;
        const bool master_samples=boundary==Boundary::master_tick;
        try {
            while (tick<target) {
                Tick next=master_samples ? tick+1 : target;
                if (next_ppu) next=std::min(next,*next_ppu);
                if (next_cpu) next=std::min(next,*next_cpu);
                if (next_event<scheduled.size()) next=std::min(next,scheduled[next_event].tick);
                active_tick=next;
                if (master_samples) sample(Phase::before,next);
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
                    const bool video_samples=boundary==Boundary::scanline || boundary==Boundary::frame;
                    if (video_samples) {
                        const auto state=ppu.state();
                        if (boundary==Boundary::scanline && state.dot==0) sample(Phase::before,state.dots+1);
                        if (boundary==Boundary::frame && state.scanline==241 && state.dot==1)
                            sample(Phase::before,frames+1);
                    }
                    ppu.clock();
                    increment(next_ppu,4);
                    route_nmi();
                    if (ppu.frame().number!=frames) record(TraceKind::frame,0,ppu.frame().number);
                    if (video_samples) {
                        const auto state=ppu.state();
                        if (boundary==Boundary::scanline && state.dot==1) sample(Phase::after,state.dots);
                        if (boundary==Boundary::frame && ppu.frame().number!=frames)
                            sample(Phase::after,ppu.frame().number);
                    }
                }
                if (next_cpu && *next_cpu==next) {
                    const auto cycle=cpu.cycles()+1;
                    if (boundary==Boundary::cpu_cycle ||
                        (boundary==Boundary::instruction && cpu.at_instruction_boundary()))
                        sample(Phase::before,cycle);
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
                    if (boundary==Boundary::cpu_cycle ||
                        (boundary==Boundary::instruction && cpu.at_instruction_boundary()))
                        sample(Phase::after,cycle);
                }
                tick=next;
                if (master_samples) sample(Phase::after,next);
            }
        } catch (...) { faulted=true; throw; }
    }
    template<bool sample_boundaries>
    const video::PpuFrame& run_frame() {
        healthy();
        const auto current=ppu.frame().number;
        while (ppu.frame().number==current) {
            if (!next_ppu) throw std::overflow_error("NES clock exhausted");
            advance_to<sample_boundaries>(*next_ppu);
        }
        return ppu.frame();
    }
    bool sampling() const noexcept {return stream && stream->options.boundary.has_value();}
};

Machine::Machine(Cartridge cartridge, Profile profile)
    : initial_(std::move(cartridge)), profile_(profile), impl_(std::make_unique<Impl>(initial_,profile)) {}
Machine::~Machine() = default;
void Machine::advance_to(Tick target) {
    if (impl_->sampling()) impl_->advance_to<true>(target);
    else impl_->advance_to<false>(target);
}
const video::PpuFrame& Machine::run_frame() {
    if (impl_->sampling()) return impl_->run_frame<true>();
    return impl_->run_frame<false>();
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
InspectionInfo Machine::schema() const noexcept { return {values,event_types,boundaries}; }
std::optional<std::uint64_t> Machine::peek(std::string_view id, std::uint32_t address) const {
    const auto index=find_id(schema().values,id);
    validate_range(values[index],address,1);
    return impl_->inspect(index,address);
}
void Machine::read(std::string_view id, std::uint32_t first,
                   std::span<std::optional<std::uint64_t>> output) const {
    const auto index=find_id(schema().values,id);
    validate_range(values[index],first,output.size());
    for(auto& value:output) value=impl_->inspect(index,first++);
}
void Machine::subscribe(const Subscription& subscription) {
    if (!subscription.capacity || (!subscription.events && !subscription.boundary) ||
        (!subscription.events && (!subscription.event_id.empty() || subscription.addresses)) ||
        (subscription.delta && !subscription.boundary) ||
        (subscription.boundary && std::ranges::find(boundaries,*subscription.boundary)==boundaries.end()) ||
        (subscription.phase!=Phase::before && subscription.phase!=Phase::after))
        throw std::invalid_argument("Invalid inspection subscription");
    auto stream=std::make_unique<Stream>();
    stream->options=subscription;
    if (!subscription.event_id.empty())
        stream->options.event_id=event_types[find_id(schema().events,subscription.event_id)].id;
    if (auto& range=stream->options.addresses) {
        const auto& info=values[find_id(schema().values,range->id)];
        if(!range->count || (info.id!="cpu.bus" && info.id!="ppu.bus"))
            throw std::invalid_argument("Expected a nonempty mapped event address range");
        validate_range(info,range->first,range->count);
        range->id=info.id;
    }
    if (auto& range=stream->options.delta) {
        stream->delta_index=find_id(schema().values,range->id);
        const auto& info=values[stream->delta_index];
        if (!range->count) throw std::invalid_argument("Empty inspection delta range");
        validate_range(info,range->first,range->count);
        range->id=info.id;
        stream->previous.resize(range->count);
    }
    stream->records.reserve(subscription.capacity);
    impl_->recording=profile_.trace || subscription.events;
    impl_->stream=std::move(stream);
}
void Machine::unsubscribe() noexcept {
    impl_->stream.reset();
    impl_->recording=profile_.trace;
}
ObservationBatch Machine::drain() {
    if (!impl_->stream) return {};
    auto& stream=*impl_->stream;
    ObservationBatch batch{stream.records,stream.lost};
    stream.records.clear();
    stream.lost=0;
    return batch;
}
std::span<const TraceEvent> Machine::trace() const noexcept { return impl_->events; }
audio::ApuState Machine::apu_state() const noexcept { return impl_->apu.state(); }
std::vector<std::int16_t> Machine::take_audio() { std::vector<std::int16_t> result; result.swap(impl_->samples); return result; }
} // namespace rse::nes
