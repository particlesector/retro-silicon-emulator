// SPDX-License-Identifier: Apache-2.0
#include <rse/session/batch.hpp>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>

namespace rse::session {
namespace {
constexpr std::size_t max_environments=256, max_workers=64;
constexpr std::size_t max_values=4096, max_frame_bytes=4*1024*1024;

// A fixed set of workers consume one synchronous job. Only one caller can
// publish a job. The stop-aware wait also handles a partial thread startup.
class Workers {
public:
    explicit Workers(std::size_t count) {
        if(count==1) return; // Serial baseline runs on the calling thread.
        threads_.reserve(count);
        for(std::size_t i=0;i<count;++i)
            threads_.emplace_back([this](std::stop_token stop){work(stop);});
    }
    void run(std::size_t count,const std::function<void(std::size_t)>& task) {
        if(threads_.empty()) {
            for(std::size_t i=0;i<count;++i) task(i);
            return;
        }
        std::unique_lock lock(mutex_);
        count_=count;
        task_=&task;
        next_.store(0,std::memory_order_relaxed);
        waiting_=threads_.size();
        ++generation_;
        ready_.notify_all();
        done_.wait(lock,[this]{return !waiting_;});
        task_=nullptr;
    }
private:
    void work(std::stop_token stop) {
        std::uint64_t generation=0;
        std::unique_lock lock(mutex_);
        while(ready_.wait(lock,stop,[&]{return generation!=generation_;})) {
            if(stop.stop_requested()) break;
            generation=generation_;
            lock.unlock();
            for(auto index=next_.fetch_add(1,std::memory_order_relaxed);index<count_;
                index=next_.fetch_add(1,std::memory_order_relaxed)) (*task_)(index);
            lock.lock();
            if(!--waiting_) done_.notify_one();
        }
    }
    std::mutex mutex_;
    std::condition_variable_any ready_;
    std::condition_variable done_;
    std::atomic_size_t next_{};
    std::size_t count_{},waiting_{};
    std::uint64_t generation_{};
    const std::function<void(std::size_t)>* task_{};
    std::vector<std::jthread> threads_;
};

Tick audio_chunk(const SystemInfo& info) {
    if(!info.audio) return std::numeric_limits<Tick>::max();
    const auto& audio=*info.audio;
    const auto clock=info.clock_hz;
    if(!audio.sample_rate || !audio.channels || audio.max_buffered_samples<audio.channels ||
        !clock.numerator || !clock.denominator)
        throw std::invalid_argument("Invalid batch session clock or audio metadata");
    // Leave half the queue as headroom for sample phase and channel rounding.
    // This sizes host drains only. Integer emulated time still defines each step.
    const long double ticks=static_cast<long double>(clock.numerator)*
        (audio.max_buffered_samples/audio.channels)/clock.denominator/audio.sample_rate/2;
    if(ticks>=static_cast<long double>(std::numeric_limits<Tick>::max()))
        return std::numeric_limits<Tick>::max();
    return std::max<Tick>(1,static_cast<Tick>(ticks));
}
}

struct Batch::Impl {
    struct Slot {
        std::unique_ptr<Session> session;
        std::uint64_t seed;
        Tick drain_interval;
    };
    explicit Impl(BatchOptions configuration,Factory make)
        : options(std::move(configuration)),factory(std::move(make)),
          workers(validate(options,factory)) {
        slots.reserve(options.environments);
        for(std::size_t i=0;i<options.environments;++i)
            slots.push_back(make_slot(i,derive_seed(options.seed,i)));
    }
    static std::size_t validate(const BatchOptions& opt,const Factory& make) {
        if(!make || !opt.environments || opt.environments>max_environments || !opt.workers ||
            opt.workers>max_workers || opt.workers>opt.environments)
            throw std::invalid_argument("Invalid batch size, worker count or factory");
        std::size_t total=0;
        for(const auto& range:opt.reads) {
            if(range.id.empty() || !range.count || range.count>max_values-total)
                throw std::invalid_argument("Invalid batch observation size");
            total+=range.count;
        }
        return opt.workers;
    }
    Slot make_slot(std::size_t index,std::uint64_t seed) const {
        auto session=factory(index,seed);
        if(!session) throw std::invalid_argument("Batch factory returned no session");
        if(!options.reads.empty()) {
            auto* inspection=session->inspection();
            if(!inspection) throw UnsupportedOperation("Batch inspection is unavailable");
            const auto schema=inspection->schema();
            for(const auto& range:options.reads) {
                const auto info=std::ranges::find(schema.values,range.id,&ValueInfo::id);
                if(info==schema.values.end() || range.first>=info->extent || range.count>info->extent-range.first)
                    throw std::invalid_argument("Invalid batch observation range");
            }
        }
        const auto interval=audio_chunk(session->info());
        return {std::move(session),seed,interval};
    }
    class Command {
    public:
        explicit Command(const Impl& owner):owner_(owner) {
            if(owner_.busy.test_and_set(std::memory_order_acquire))
                throw std::logic_error("Another batch command is active");
        }
        ~Command() {owner_.busy.clear(std::memory_order_release);}
    private:
        const Impl& owner_;
    };
    void step_slot(std::size_t index,Tick ticks,const std::optional<BatchAction>& action,BatchResult& result) {
        const auto& slot=slots[index];
        auto& session=*slot.session;
        try {
            auto current=session.status();
            if(current.faulted) throw std::logic_error("Batch slot requires reset");
            if(ticks>std::numeric_limits<Tick>::max()-current.tick)
                throw std::overflow_error("Batch step exceeds available tick range");
            if(action) session.schedule_input(current.tick+1,action->port,action->pressed);
            const bool audio=session.info().audio.has_value();
            while(ticks) {
                const auto amount=std::min(ticks,slot.drain_interval);
                current.tick+=amount;
                session.advance_to(current.tick);
                if(audio) (void)session.take_audio();
                ticks-=amount;
            }
            auto offset=std::size_t{0};
            auto* inspection=options.reads.empty() ? nullptr : session.inspection();
            if(!inspection && !options.reads.empty())
                throw UnsupportedOperation("Batch inspection is unavailable after advancement");
            for(const auto& range:options.reads) {
                auto output=std::span(result.values).subspan(offset,range.count);
                inspection->read(range.id,range.first,output);
                offset+=range.count;
            }
            if(options.video) {
                if(const auto frame=session.video()) {
                    if(frame->pixels.size()>max_frame_bytes) throw std::length_error("Batch frame exceeds size limit");
                    result.frame=BatchFrame{frame->number,frame->pitch_bytes,
                        {frame->pixels.begin(),frame->pixels.end()}};
                }
            }
        } catch(...) {
            result.error=std::current_exception();
            result.values.clear();
            result.frame.reset();
        }
        result.status=session.status();
    }
    BatchOptions options;
    Factory factory;
    mutable std::atomic_flag busy{};
    std::vector<Slot> slots;
    Workers workers;
};

Batch::Batch(BatchOptions options,Factory factory)
    :impl_(std::make_unique<Impl>(std::move(options),std::move(factory))) {}
Batch::~Batch()=default;

std::uint64_t Batch::derive_seed(std::uint64_t seed,std::size_t index) noexcept {
    auto value=seed+0x9e3779b97f4a7c15ull*(std::uint64_t(index)+1);
    value=(value^(value>>30))*0xbf58476d1ce4e5b9ull;
    value=(value^(value>>27))*0x94d049bb133111ebull;
    return value^(value>>31);
}
void Batch::reset(std::size_t index,std::optional<std::uint64_t> seed) {
    const Impl::Command command(*impl_);
    auto& slot=impl_->slots.at(index);
    slot=impl_->make_slot(index,seed.value_or(slot.seed));
}
std::uint64_t Batch::seed(std::size_t index) const {
    const Impl::Command command(*impl_);
    return impl_->slots.at(index).seed;
}
std::vector<BatchResult> Batch::step(Tick ticks,std::span<const std::optional<BatchAction>> actions) {
    const Impl::Command command(*impl_);
    const auto count=impl_->slots.size();
    if(!ticks || (!actions.empty() && actions.size()!=count))
        throw std::invalid_argument("Invalid batch step size or action count");
    std::size_t value_count=0;
    for(const auto& range:impl_->options.reads) value_count+=range.count;
    std::vector<BatchResult> result(count);
    for(auto& slot:result) slot.values.resize(value_count);
    const std::optional<BatchAction> none;
    impl_->workers.run(count,[&](std::size_t index) noexcept {
        impl_->step_slot(index,ticks,actions.empty() ? none : actions[index],result[index]);
    });
    return result;
}
std::vector<std::uint8_t> Batch::snapshot(std::size_t index) const {
    const Impl::Command command(*impl_);
    return impl_->slots.at(index).session->snapshot();
}
}
