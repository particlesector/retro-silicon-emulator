// SPDX-License-Identifier: Apache-2.0
#include "acceptance_programs.hpp"
#include <rse/core/state_codec.hpp>
#include <rse/session/batch.hpp>
#include <rse/session/builtins.hpp>
#include <array>
#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <iostream>
#include <mutex>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#elif defined(__linux__)
#include <sys/resource.h>
#endif

namespace {
using namespace rse::session;
using rse::Tick;
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
template<class Error> void reject(auto action) {
    try {action();}
    catch(const Error&) {return;}
    throw std::runtime_error("Expected batch request rejection");
}
class Fake final : public Session {
public:
    explicit Fake(std::uint64_t seed,std::function<void()> on_advance={})
        :seed_(seed),on_advance_(std::move(on_advance)) {}
    const SystemInfo& info() const noexcept override {return info_;}
    Status status() const noexcept override {return {.tick=tick_};}
    void reset() override {tick_=0; pending_=0;}
    void advance_to(Tick tick) override {
        if(on_advance_) on_advance_();
        if(tick-tick_+pending_>2) throw std::logic_error("Fake audio queue overflow");
        pending_+=tick-tick_;
        tick_=tick;
    }
    std::vector<std::int16_t> take_audio() override {
        std::vector<std::int16_t> output(pending_);
        pending_=0;
        return output;
    }
    std::vector<std::uint8_t> snapshot() const override {
        rse::state::Writer out;
        out(tick_,seed_,pending_);
        return std::move(out.bytes);
    }
private:
    SystemInfo info_{"fake","Fake",{1000,1},{},AudioFormat{1000,1,SampleFormat::signed16_native,2},{},false};
    Tick tick_{},pending_{};
    std::uint64_t seed_;
    std::function<void()> on_advance_;
};
void fake() {
    Batch batch({.environments=2,.workers=2,.seed=42},
        [](std::size_t,std::uint64_t seed){
            if(seed==9) throw std::invalid_argument("Invalid fake seed");
            return std::make_unique<Fake>(seed);
        });
    check(batch.seed(0)==Batch::derive_seed(42,0) && batch.seed(0)!=batch.seed(1),"independent fake seeds");
    const auto initial=batch.snapshot(0);
    reject<std::invalid_argument>([&]{(void)batch.step(0);});
    reject<std::invalid_argument>([&]{(void)batch.step(1,std::array<std::optional<BatchAction>,1>{});});
    check(batch.snapshot(0)==initial,"invalid batch changed state");
    const auto results=batch.step(100);
    for(const auto& result:results) check(!result.error && result.status.tick==100 && !result.frame,
        "fake audio drains or headless output");
    const auto other=batch.snapshot(1);
    batch.reset(0);
    check(batch.snapshot(0)==initial && batch.snapshot(1)==other,"reset affected another slot");
    batch.reset(0,7);
    check(batch.seed(0)==7 && batch.snapshot(0)!=initial && batch.snapshot(1)==other,
        "explicit seed replacement");
    const auto replacement=batch.snapshot(0);
    reject<std::invalid_argument>([&]{batch.reset(0,9);});
    check(batch.seed(0)==7 && batch.snapshot(0)==replacement && batch.snapshot(1)==other,
        "failed reset replaced live slot");
}
void worker_overlap() {
    std::mutex mutex;
    std::condition_variable ready;
    unsigned arrivals=0;
    Batch* owner=nullptr;
    Batch batch({.environments=2,.workers=2},
        [&](std::size_t,std::uint64_t seed) {
            return std::make_unique<Fake>(seed,[&] {
                reject<std::logic_error>([&]{(void)owner->seed(0);});
                std::unique_lock lock(mutex);
                ++arrivals;
                ready.notify_all();
                check(ready.wait_for(lock,std::chrono::seconds(5),[&]{return arrivals==2;}),
                    "slots did not execute concurrently");
            });
        });
    owner=&batch;
    const auto results=batch.step(1);
    check(!results[0].error && !results[1].error && arrivals==2,"worker overlap or reentrant guard");
}
std::vector<std::uint8_t> av_image() {return rse::acceptance::image(rse::acceptance::av_input_program());}
constexpr std::array<std::string_view,1> a_button{"a"};
constexpr std::array<std::string_view,1> b_button{"b"};
auto action(std::uint64_t seed,unsigned step) {
    const auto mixed=Batch::derive_seed(seed,step);
    return BatchAction{"controller-1",mixed&1 ? std::span<const std::string_view>(a_button) :
        std::span<const std::string_view>(b_button)};
}
void nes() {
    const auto image=av_image();
    const auto factory=[&](std::size_t,std::uint64_t) {return create("nes-ntsc",image);};
    Batch parallel({.environments=4,.workers=3,.seed=7,.reads={{"cpu.ram",0,32}},.video=true},factory);
    Batch serial({.environments=4,.workers=1,.seed=7,.reads={{"cpu.ram",0,32}},.video=true},factory);
    Batch suppressed({.environments=1,.workers=1,.seed=7},factory);
    std::array<std::optional<BatchAction>,4> actions;
    std::optional<BatchFrame> owned;
    for(unsigned step=0;step<6;++step) {
        for(std::size_t i=0;i<actions.size();++i) actions[i]=action(parallel.seed(i),step);
        const auto p=parallel.step(360'000,actions);
        const auto s=serial.step(360'000,actions);
        const auto quiet=suppressed.step(360'000,std::span(actions).first(1));
        check(!quiet[0].error && !quiet[0].frame,"suppressed host frame was copied");
        for(std::size_t i=0;i<p.size();++i) {
            check(!p[i].error && !s[i].error && p[i].status.tick==s[i].status.tick &&
                p[i].status.video_frames==s[i].status.video_frames && p[i].values==s[i].values &&
                p[i].frame && s[i].frame && p[i].frame->number==s[i].frame->number &&
                p[i].frame->pitch_bytes==s[i].frame->pitch_bytes &&
                p[i].frame->pixels==s[i].frame->pixels,"parallel NES observations diverged");
        }
        if(!owned) owned=p.front().frame;
    }
    check(owned && owned->number==1 && owned->pixels.size()==256*240*2,"owned first frame lifetime");
    for(std::size_t i=0;i<actions.size();++i)
        check(parallel.snapshot(i)==serial.snapshot(i),"serial/parallel canonical state differs");
    check(suppressed.snapshot(0)==parallel.snapshot(0),"suppression changed machine state");

    const std::array<std::string_view,1> bad{"invalid"};
    const auto before=parallel.snapshot(0);
    actions[0]=BatchAction{"controller-1",bad};
    const auto failed=parallel.step(1,actions);
    check(bool(failed[0].error) && !failed[1].error && failed[1].status.tick==6*360'000+1 &&
        parallel.snapshot(0)==before,"invalid slot input affected another slot");
    parallel.reset(0);
    check(parallel.snapshot(0)!=before && parallel.seed(0)==serial.seed(0),"NES seed or reset");

    // A single request spans more than the NES adapter's one-second host queue.
    Batch draining({.environments=1,.workers=1},factory);
    check(!draining.step(22'000'000)[0].error,"long batch overflowed the audio queue");

    NesProgram bad_rom;
    bad_rom.emit({0x02});
    const auto invalid=rse::acceptance::image(bad_rom);
    Batch isolated({.environments=2,.workers=2},
        [&](std::size_t i,std::uint64_t){return create("nes-ntsc",i ? image : invalid);});
    const auto fault=isolated.step(200);
    check(fault[0].error && fault[0].status.faulted && !fault[1].error &&
        fault[1].status.tick==200,"execution fault escaped its batch slot");
    isolated.reset(0);
    check(!isolated.snapshot(0).empty(),"faulted slot could not reset");
}
struct Usage {double cpu_seconds{};std::uint64_t peak_kib{};};
Usage usage() {
#ifdef _WIN32
    FILETIME creation{},exit{},kernel{},user{};
    PROCESS_MEMORY_COUNTERS memory{};
    memory.cb=sizeof(memory);
    const auto process=GetCurrentProcess();
    check(GetProcessTimes(process,&creation,&exit,&kernel,&user) &&
        K32GetProcessMemoryInfo(process,&memory,sizeof(memory)),"Windows process metrics unavailable");
    const auto seconds=[](FILETIME t){return ((std::uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime)/1e7;};
    return {seconds(kernel)+seconds(user),memory.PeakWorkingSetSize/1024};
#elif defined(__linux__)
    rusage u{};
    check(getrusage(RUSAGE_SELF,&u)==0,"Linux process metrics unavailable");
    const auto seconds=[](timeval t){return t.tv_sec+t.tv_usec*1e-6;};
    return {seconds(u.ru_utime)+seconds(u.ru_stime),static_cast<std::uint64_t>(u.ru_maxrss)};
#else
    return {};
#endif
}
void benchmark(std::size_t count,std::size_t workers,unsigned steps) {
    const auto image=av_image();
    Batch batch({.environments=count,.workers=workers,.seed=2026,.reads={{"cpu.ram",0,32}},.video=true},
        [&](std::size_t,std::uint64_t){return create("nes-ntsc",image);});
    std::vector<std::optional<BatchAction>> actions(count);
    std::vector<std::uint64_t> seeds(count);
    for(std::size_t i=0;i<count;++i) seeds[i]=batch.seed(i);
    constexpr Tick ticks=357'368;
    std::uint64_t frames=0,bytes=0;
    const auto cpu_start=usage().cpu_seconds;
    const auto start=std::chrono::steady_clock::now();
    for(unsigned step=0;step<steps;++step) {
        frames=0;
        for(std::size_t i=0;i<count;++i) actions[i]=action(seeds[i],step);
        for(const auto& result:batch.step(ticks,actions)) {
            if(result.error) std::rethrow_exception(result.error);
            frames+=result.status.video_frames;
            bytes+=9*result.values.size();
            if(result.frame) bytes+=result.frame->pixels.size();
        }
    }
    const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    const auto metrics=usage();
    rse::state::Writer hashes;
    for(std::size_t i=0;i<count;++i) hashes(rse::state::hash(batch.snapshot(i)));
    std::cout<<std::fixed<<std::setprecision(3)<<count<<','<<workers<<','<<elapsed<<','
        <<frames/elapsed<<','<<count*steps*ticks/elapsed<<','<<count*steps/elapsed<<','
        <<(metrics.cpu_seconds-cpu_start)/elapsed*100<<','<<metrics.peak_kib<<','
        <<bytes/elapsed/1048576<<','<<std::hex<<rse::state::hash(hashes.bytes)<<'\n';
}
}
int main(int argc,char** argv) {
    try {
        if(argc==1) {fake();worker_overlap();nes();}
        else if(argc==5 && std::string_view(argv[1])=="--benchmark") {
            const auto steps=std::stoul(argv[4]);
            check(steps && steps<=10000,"Benchmark step count out of bounds");
            benchmark(std::stoul(argv[2]),std::stoul(argv[3]),static_cast<unsigned>(steps));
        } else throw std::invalid_argument("Usage: rse-batch-tests [--benchmark environments workers steps]");
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
