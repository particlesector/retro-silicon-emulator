// SPDX-License-Identifier: Apache-2.0
#include <rse/session/automation.hpp>
#include <algorithm>

namespace rse::session {
namespace {
constexpr std::size_t max_capacity=65'536;
constexpr Tick pause_interval=256;

void validate_wire_id(std::string_view id) {
    if(id.size()>255 || std::ranges::any_of(id,[](unsigned char c){return !c || c>127;}))
        throw std::invalid_argument("Observation ID is incompatible with binary format v1");
}

template<class Info>
const Info& find(std::span<const Info> items,std::string_view id) {
    const auto found=std::ranges::find(items,id,&Info::id);
    if(found==items.end()) throw std::invalid_argument("Unknown automation inspection ID");
    return *found;
}

// Wire enum values are explicit and independent of the C++ enum ordinals.
unsigned wire_kind(RecordKind kind) {
    switch(kind) {
    case RecordKind::event: return 0;
    case RecordKind::boundary: return 1;
    case RecordKind::delta: return 2;
    }
    throw std::logic_error("Invalid observation record kind");
}
void append(std::vector<std::byte>& out,std::uint64_t value,unsigned bytes) {
    for(unsigned i=0;i<bytes;++i) {
        out.push_back(static_cast<std::byte>(value&255));
        value>>=8;
    }
}
class BatchWriter {
public:
    explicit BatchWriter(std::size_t capacity):capacity_(capacity) {
        if(!capacity_) return;
        // Fixed maximum encoded ID size and record count bound storage.
        bytes_.reserve(20+capacity_*301);
        for(const char c:std::string_view("RSEO")) bytes_.push_back(static_cast<std::byte>(c));
        append(bytes_,1,2);
        append(bytes_,0,2);
        append(bytes_,0,4);
        append(bytes_,0,8);
    }
    void add(const ObservationBatch& batch) {
        if(!capacity_) return;
        lost_+=batch.lost;
        for(const auto& record:batch.records) {
            if(count_==capacity_) {++lost_; continue;}
            if(record.id.size()>255) throw std::logic_error("Observation ID exceeds binary format limit");
            append(bytes_,record.sequence,8);
            append(bytes_,record.tick,8);
            append(bytes_,record.index,8);
            append(bytes_,wire_kind(record.kind),1);
            append(bytes_,record.phase==Phase::after,1);
            append(bytes_,unsigned(record.address.has_value())|unsigned(record.value.has_value())<<1|
                unsigned(record.previous.has_value())<<2|unsigned(record.initial)<<3,1);
            append(bytes_,record.id.size(),1);
            append(bytes_,record.address.value_or(0),4);
            append(bytes_,record.value.value_or(0),8);
            append(bytes_,record.previous.value_or(0),8);
            for(const unsigned char c:record.id) bytes_.push_back(static_cast<std::byte>(c));
            ++count_;
        }
    }
    [[nodiscard]] std::uint64_t lost() const noexcept {return lost_;}
    std::vector<std::byte> finish() {
        if(capacity_) {
            for(unsigned i=0;i<4;++i) bytes_[8+i]=static_cast<std::byte>((count_>>(8*i))&255);
            for(unsigned i=0;i<8;++i) bytes_[12+i]=static_cast<std::byte>((lost_>>(8*i))&255);
        }
        return std::move(bytes_);
    }
private:
    std::size_t capacity_,count_{};
    std::uint64_t lost_{};
    std::vector<std::byte> bytes_;
};
struct Unsubscribe {
    Inspection* inspection;
    ~Unsubscribe() {if(inspection) inspection->unsubscribe();}
};
}

class Automation::Command {
public:
    explicit Command(const Automation& owner):owner_(owner) {
        if(owner_.busy_.test_and_set(std::memory_order_acquire))
            throw std::logic_error("Another automation command is active");
    }
    ~Command() {owner_.busy_.clear(std::memory_order_release);}
private:
    const Automation& owner_;
};
Automation::Automation(std::unique_ptr<Session> session):session_(std::move(session)) {
    if(!session_) throw std::invalid_argument("Automation requires a session");
}
Inspection& Automation::inspection() const {
    auto* capability=session_->inspection();
    if(!capability) throw UnsupportedOperation("Session inspection is unavailable");
    return *capability;
}
Status Automation::status() const {const Command command(*this); return session_->status();}
const SystemInfo& Automation::info() const {const Command command(*this); return session_->info();}
InspectionInfo Automation::schema() const {const Command command(*this); return inspection().schema();}
std::optional<std::uint64_t> Automation::peek(std::string_view id,std::uint32_t address) const {
    const Command command(*this);
    return inspection().peek(id,address);
}
void Automation::read(std::string_view id,std::uint32_t first,std::span<std::optional<std::uint64_t>> output) const {
    const Command command(*this);
    inspection().read(id,first,output);
}
void Automation::reset() {const Command command(*this); session_->reset();}
std::vector<std::uint8_t> Automation::snapshot() const {
    const Command command(*this);
    return session_->snapshot();
}
void Automation::restore(std::span<const std::uint8_t> bytes) {
    const Command command(*this);
    session_->restore(bytes);
}
void Automation::schedule_input(Tick tick,std::string_view port,std::span<const std::string_view> pressed) {
    const Command command(*this);
    session_->schedule_input(tick,port,pressed);
}
std::vector<std::int16_t> Automation::take_audio() {
    const Command command(*this);
    return session_->take_audio();
}
void Automation::pause() noexcept {paused_.store(true,std::memory_order_release);}
void Automation::resume() {const Command command(*this); paused_.store(false,std::memory_order_release);}

RunResult Automation::run(const RunRequest& request) {
    const Command command(*this);
    const auto start=session_->status();
    if(!request.budget || request.target<start.tick || request.observation_capacity>max_capacity ||
        (request.delivery!=Delivery::lossless && request.delivery!=Delivery::preview))
        throw std::invalid_argument("Invalid automation tick budget, target or delivery");
    if(start.faulted) throw std::logic_error("Session requires reset after failure");
    const bool controlled=request.step || request.breakpoint || request.watchpoint;
    Inspection* observe=controlled || request.observation_capacity ? &inspection() : nullptr;
    const auto boundary=request.breakpoint ? std::optional{request.breakpoint->boundary} : request.step;
    if(request.step && request.breakpoint)
        throw std::invalid_argument("Choose either step or breakpoint");
    Subscription options{};
    if(observe) {
        const auto schema=observe->schema();
        if(request.observation_capacity)
            for(const auto& event:schema.events) validate_wire_id(event.id);
        if(boundary && std::ranges::find(schema.boundaries,*boundary)==schema.boundaries.end())
            throw UnsupportedOperation("Requested boundary is unavailable");
        if(request.breakpoint) {
            const auto& point=*request.breakpoint;
            const auto& info=find(schema.values,point.value.id);
            if(request.observation_capacity) validate_wire_id(info.id);
            if(point.value.count!=1 || point.value.first>=info.extent ||
                (info.bits<64 && point.equals>=(std::uint64_t{1}<<info.bits)))
                throw std::invalid_argument("Invalid breakpoint value");
            options.delta=point.value;
        }
        if(request.watchpoint) {
            const auto& point=*request.watchpoint;
            const auto& event=find(schema.events,point.event_id);
            const auto& range=point.addresses;
            if(event.address_domain.empty() || event.address_domain!=range.id)
                throw std::invalid_argument("Watchpoint event does not use the selected address domain");
            const auto& domain=find(schema.values,range.id);
            if(!range.count || range.first>=domain.extent || range.count>domain.extent-range.first)
                throw std::invalid_argument("Invalid watchpoint address range");
            if(!request.observation_capacity) {
                options.event_id=point.event_id;
                options.addresses=range;
            }
        }
        options.capacity=std::max<std::size_t>(request.observation_capacity,controlled ? 256 : 1);
        options.events=request.watchpoint.has_value() || request.observation_capacity!=0;
        options.boundary=boundary;
    }
    BatchWriter writer(request.observation_capacity);
    if(observe) observe->subscribe(options);
    const Unsubscribe subscription{observe};
    const auto end=start.tick+std::min(request.target-start.tick,request.budget);
    auto tick=start.tick;
    const auto finish=[&](StopReason reason) {return RunResult{tick,reason,writer.finish()};};
    while(tick<end) {
        if(paused_.load(std::memory_order_acquire)) return finish(StopReason::paused);
        // A whole master tick is the smallest safe stop. Using single ticks
        // with stop controls prevents execution beyond the first match.
        const auto amount=controlled ? Tick{1} : pause_interval;
        tick+=std::min(end-tick,amount);
        session_->advance_to(tick);
        if(!observe) continue;
        const auto batch=observe->drain();
        writer.add(batch);
        if((controlled && batch.lost) || (writer.lost() && request.delivery==Delivery::lossless))
            return finish(StopReason::overflow);
        for(const auto& record:batch.records) {
            if(request.watchpoint && record.kind==RecordKind::event && record.address &&
                record.id==request.watchpoint->event_id && *record.address>=request.watchpoint->addresses.first &&
                *record.address-request.watchpoint->addresses.first<request.watchpoint->addresses.count)
                return finish(StopReason::watchpoint);
            if(request.breakpoint && record.kind==RecordKind::delta && record.value==request.breakpoint->equals)
                return finish(StopReason::breakpoint);
            if(request.step && record.kind==RecordKind::boundary) return finish(StopReason::step);
        }
    }
    return finish(tick==request.target ? StopReason::target : StopReason::budget);
}
}
