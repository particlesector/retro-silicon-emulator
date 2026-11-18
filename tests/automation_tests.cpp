// SPDX-License-Identifier: Apache-2.0
#include "acceptance_programs.hpp"
#include <rse/session/automation.hpp>
#include <rse/session/builtins.hpp>
#include <iostream>
#include <limits>
#include <utility>

namespace {
using namespace rse::session;
void check(bool good,const char* message) {if(!good) throw std::runtime_error(message);}
template<class Error,class Action>
void rejects(Action action) {
    try {action();} catch(const Error&) {return;}
    throw std::runtime_error("Expected automation request rejection");
}
class Fake final : public Session, public Inspection {
public:
    const SystemInfo& info() const noexcept override {return info_;}
    Status status() const noexcept override {return status_;}
    Inspection* inspection() noexcept override {return inspectable ? this : nullptr;}
    void reset() override {status_={};}
    void advance_to(rse::Tick target) override {
        check(target>=status_.tick,"invalid fake advancement");
        if(owner && !status_.tick) {
            rejects<std::logic_error>([&]{owner->reset();});
            owner->pause();
        }
        status_.tick=target;
        if(subscribed) records_.push_back({++sequence_,target,RecordKind::boundary,
            Phase::after,{},{},{},{},target});
    }
    InspectionInfo schema() const noexcept override {
        static constexpr std::array supported{Boundary::master_tick};
        static constexpr std::array invalid_events{EventInfo{"fake.\xff",ClockDomain::master,{},1}};
        return {{},invalid_wire_id ? std::span<const EventInfo>(invalid_events) : std::span<const EventInfo>{},supported};
    }
    std::optional<std::uint64_t> peek(std::string_view,std::uint32_t) const override {
        throw UnsupportedOperation("Fake has no values");
    }
    void read(std::string_view,std::uint32_t,std::span<std::optional<std::uint64_t>>) const override {
        throw UnsupportedOperation("Fake has no values");
    }
    void subscribe(const Subscription& options) override {
        check(options.boundary==Boundary::master_tick && !options.events,"unexpected fake subscription");
        records_.clear();
        sequence_=0;
        subscribed=true;
    }
    void unsubscribe() noexcept override {subscribed=false;}
    ObservationBatch drain() override {return {std::exchange(records_,{}),0};}
    Automation* owner{};
    bool inspectable{};
    bool invalid_wire_id{};
private:
    SystemInfo info_{"fake","Fake",{1000,3},{},{},{},false};
    Status status_{};
    bool subscribed{};
    std::uint64_t sequence_{};
    std::vector<ObservationRecord> records_;
};
std::uint64_t read_int(std::span<const std::byte> bytes,std::size_t offset,unsigned count) {
    check(offset+count<=bytes.size(),"truncated observation batch");
    std::uint64_t value=0;
    for(unsigned i=0;i<count;++i)
        value|=std::uint64_t(std::to_integer<unsigned>(bytes[offset+i]))<<(8*i);
    return value;
}
void fake_session() {
    auto fake=std::make_unique<Fake>();
    auto* implementation=fake.get();
    Automation owner(std::move(fake));
    implementation->owner=&owner;
    rejects<UnsupportedOperation>([&]{(void)owner.schema();});
    rejects<UnsupportedOperation>([&]{(void)owner.run({.target=1,.budget=1,.step=Boundary::master_tick});});
    auto result=owner.run({.target=10'000,.budget=10'000});
    check(result.reason==StopReason::paused && result.tick>0 && result.tick<10'000,"cooperative pause");
    check(owner.run({.target=10'000,.budget=100}).tick==result.tick,"paused run moved");
    owner.resume();
    const auto next=owner.run({.target=10'000,.budget=13});
    check(next.reason==StopReason::budget && next.tick==result.tick+13,"fake tick budget");
    owner.reset();
    check(owner.status().tick==0,"fake reset");
    const auto limit=std::numeric_limits<rse::Tick>::max();
    // A target near the integer limit must not overflow the budget calculation.
    implementation->owner=nullptr;
    check(owner.run({.target=limit,.budget=2}).tick==2,"maximum target");
    implementation->inspectable=true;
    const auto step=owner.run({.target=10,.budget=8,.step=Boundary::master_tick});
    check(step.tick==3 && step.reason==StopReason::step,"generic fake boundary step");
    rejects<UnsupportedOperation>([&]{(void)owner.run({.target=10,.budget=7,.step=Boundary::frame});});
    implementation->invalid_wire_id=true;
    rejects<std::invalid_argument>([&]{(void)owner.run({.target=10,.budget=7,.observation_capacity=1});});
    check(owner.status().tick==3,"invalid wire schema advanced the fake session");
}
void nes_session() {
    const auto image=rse::acceptance::image(rse::acceptance::programs()[0].second);
    Automation owner(create("nes-ntsc",image));
    check(!owner.schema().events.empty() && owner.peek("cpu.ram",0x10)==0,"NES schema and peek");
    std::array<std::optional<std::uint64_t>,2> values;
    owner.read("cpu.ram",0x10,values);
    check(values[0]==0 && values[1]==0 && owner.info().input_ports.size()==1,"NES bulk read and input discovery");
    rejects<std::invalid_argument>([&]{(void)owner.run({.target=5,.budget=0});});
    rejects<std::invalid_argument>([&]{(void)owner.run({.target=5,.budget=5,.observation_capacity=65'537});});
    rejects<std::invalid_argument>([&]{(void)owner.run({.target=5,.budget=5,
        .watchpoint=Watchpoint{"cpu.write",{"ppu.bus",0,1}}});});
    rejects<std::invalid_argument>([&]{(void)owner.run({.target=5,.budget=5,
        .breakpoint=Breakpoint{Boundary::instruction,{"cpu.a"},256}});});
    rejects<std::invalid_argument>([&]{owner.schedule_input(1,"controller-1",std::array<std::string_view,1>{"bad"});});
    check(owner.status().tick==0,"rejected request advanced NES");

    auto result=owner.run({.target=100,.budget=100,.step=Boundary::cpu_cycle});
    check(result.reason==StopReason::step && result.tick==12,"first CPU slot");
    result=owner.run({.target=200,.budget=200,
        .breakpoint=Breakpoint{Boundary::instruction,{"cpu.pc"},0x8000}});
    check(result.reason==StopReason::breakpoint && result.tick==84,"instruction boundary breakpoint");
    result=owner.run({.target=500,.budget=500,
        .watchpoint=Watchpoint{"cpu.write",{"cpu.bus",0x10,1}},.observation_capacity=64});
    check(result.reason==StopReason::watchpoint && result.tick==132,"first RMW write watchpoint");
    check(owner.peek("cpu.ram",0x10)==0,"watchpoint ran past matched write");
    check(read_int(result.observations,0,4)==0x4f455352 && read_int(result.observations,4,2)==1 &&
        read_int(result.observations,8,4)>0 && !read_int(result.observations,12,8),"binary batch header");
    check(owner.run({.target=500,.budget=12}).tick==144 && owner.peek("cpu.ram",0x10)==1,
        "resume after watchpoint");
    rejects<std::invalid_argument>([&]{(void)owner.run({.target=10,.budget=1});});

    owner.reset();
    const auto lossless=owner.run({.target=1000,.budget=1000,.observation_capacity=1});
    check(lossless.reason==StopReason::overflow && lossless.tick<1000 &&
        read_int(lossless.observations,8,4)==1 && read_int(lossless.observations,12,8)>0,
        "lossless overflow did not stop");
    owner.reset();
    const auto preview=owner.run({.target=1000,.budget=1000,.observation_capacity=1,.delivery=Delivery::preview});
    check(preview.reason==StopReason::target && preview.tick==1000 &&
        read_int(preview.observations,12,8)>read_int(lossless.observations,12,8),"preview loss or stop reason");

    const auto replay=[&] {
        owner.reset();
        const std::array<std::string_view,1> actions{"start"};
        owner.schedule_input(20,"controller-1",actions);
        const auto run=owner.run({.target=4000,.budget=4000,.observation_capacity=1024});
        check(run.reason==StopReason::target,"replay run did not reach target");
        return std::pair{run.observations,owner.take_audio()};
    };
    const auto first=replay();
    check(replay()==first,"automated replay observations or PCM differ");
}
}
int main() {
    try {fake_session();nes_session();}
    catch(const std::exception& failure) {std::cerr<<failure.what()<<'\n';return 1;}
}
