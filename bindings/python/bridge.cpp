// SPDX-License-Identifier: Apache-2.0
#include <rse/session/automation.hpp>
#include <rse/session/builtins.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <iomanip>
#include <locale>
#include <sstream>

namespace {
using namespace rse::session;
thread_local char error[512]{};

template<class Action>
int invoke(Action action) noexcept {
    try {
        action();
        return 0;
    } catch(const std::exception& failure) {
        const auto message=std::string_view(failure.what()).substr(0,sizeof(error)-1);
        std::memcpy(error,message.data(),message.size());
        error[message.size()]='\0';
    } catch(...) {
        std::strcpy(error,"Unknown native automation error");
    }
    return -1;
}
std::vector<std::byte> bytes(std::string_view text) {
    const auto data=std::as_bytes(std::span(text));
    return {data.begin(),data.end()};
}
struct Buffer {std::vector<std::byte> data;};
constexpr std::array boundaries{Boundary::master_tick,Boundary::cpu_cycle,Boundary::instruction,
    Boundary::scanline,Boundary::frame};
constexpr std::array boundary_ids{"master_tick","cpu_cycle","instruction","scanline","frame"};
Boundary boundary(int index) {return boundaries.at(index);}
std::string_view boundary_id(Boundary value) {
    return boundary_ids.at(std::ranges::find(boundaries,value)-boundaries.begin());
}
}

extern "C" {
struct RunOptions {
    std::uint64_t target,budget,equals;
    std::size_t capacity;
    const char *value_id,*event_id,*domain;
    std::uint32_t address,first,count;
    int step,breakpoint,preview;
};
const char* rse_py_error() noexcept {return error;}
int rse_py_create(const char* system,const std::uint8_t* image,std::size_t size,Automation** output) noexcept {
    return invoke([&]{*output=new Automation(create(system,{image,size}));});
}
void rse_py_destroy(Automation* owner) noexcept {delete owner;}
int rse_py_status(Automation* owner,std::uint64_t* tick,std::uint64_t* frames,int* faulted) noexcept {
    return invoke([&]{
        const auto status=owner->status();
        *tick=status.tick;
        *frames=status.video_frames;
        *faulted=status.faulted;
    });
}
int rse_py_reset(Automation* owner) noexcept {return invoke([&]{owner->reset();});}
int rse_py_save(Automation* owner,Buffer** output) noexcept {
    return invoke([&]{
        const auto snapshot=owner->snapshot();
        const auto data=std::as_bytes(std::span(snapshot));
        *output=new Buffer{{data.begin(),data.end()}};
    });
}
int rse_py_restore(Automation* owner,const std::uint8_t* data,std::size_t size) noexcept {
    return invoke([&]{owner->restore({data,size});});
}
void rse_py_pause(Automation* owner) noexcept {owner->pause();}
int rse_py_resume(Automation* owner) noexcept {return invoke([&]{owner->resume();});}
int rse_py_input(Automation* owner,std::uint64_t tick,const char* port,
                 const char* const* actions,std::size_t count) noexcept {
    return invoke([&]{
        std::vector<std::string_view> pressed;
        pressed.reserve(count);
        for(const auto* action:std::span(actions,count)) pressed.push_back(action);
        owner->schedule_input(tick,port,pressed);
    });
}
int rse_py_schema(Automation* owner,Buffer** output) noexcept {
    return invoke([&]{
        const auto schema=owner->schema();
        const auto& system=owner->info();
        // IDs in built-in schemas are ASCII identifiers.
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out<<"{\"system\":"<<std::quoted(std::string(system.id))
           <<",\"clock_hz\":["<<system.clock_hz.numerator<<','<<system.clock_hz.denominator
           <<"],\"input_ports\":{";
        const char* port_separator="";
        for(const auto& port:system.input_ports) {
            out<<port_separator<<std::quoted(std::string(port.id))<<":[";
            const char* action_separator="";
            for(const auto action:port.actions) {
                out<<action_separator<<std::quoted(std::string(action));
                action_separator=",";
            }
            out<<']';
            port_separator=",";
        }
        out<<"},\"values\":{";
        const char* separator="";
        for(const auto& info:schema.values) {
            out<<separator<<std::quoted(std::string(info.id))<<":["
               <<int(info.kind)<<','<<int(info.clock)<<','<<info.bits<<','<<info.extent<<','<<int(info.space)<<']';
            separator=",";
        }
        out<<"},\"events\":{";
        separator="";
        for(const auto& event:schema.events) {
            out<<separator<<std::quoted(std::string(event.id))<<":["<<int(event.clock)<<','
               <<std::quoted(std::string(event.address_domain))<<','<<event.data_bits<<']';
            separator=",";
        }
        out<<"},\"boundaries\":[";
        separator="";
        for(const auto value:schema.boundaries) {
            out<<separator<<std::quoted(std::string(boundary_id(value)));
            separator=",";
        }
        out<<"]}";
        *output=new Buffer{bytes(out.str())};
    });
}
int rse_py_peek(Automation* owner,const char* id,std::uint32_t address,
                std::uint64_t* value,int* available) noexcept {
    return invoke([&]{
        const auto data=owner->peek(id,address);
        *available=data.has_value();
        *value=data.value_or(0);
    });
}
int rse_py_run(Automation* owner,const RunOptions* options,std::uint64_t* tick,
               int* reason,Buffer** output) noexcept {
    return invoke([&]{
        RunRequest request{.target=options->target,.budget=options->budget,
            .observation_capacity=options->capacity,
            .delivery=options->preview ? Delivery::preview : Delivery::lossless};
        if(options->step>=0) request.step=boundary(options->step);
        if(options->breakpoint>=0)
            request.breakpoint=Breakpoint{boundary(options->breakpoint),{options->value_id,options->address},options->equals};
        if(options->event_id)
            request.watchpoint=Watchpoint{options->event_id,{options->domain,options->first,options->count}};
        auto buffer=std::make_unique<Buffer>();
        auto result=owner->run(request);
        buffer->data=std::move(result.observations);
        *tick=result.tick;
        *reason=int(result.reason);
        *output=buffer.release();
    });
}
int rse_py_audio(Automation* owner,Buffer** output) noexcept {
    return invoke([&]{
        auto buffer=std::make_unique<Buffer>();
        const auto samples=owner->take_audio();
        const auto data=std::as_bytes(std::span(samples));
        buffer->data.assign(data.begin(),data.end());
        *output=buffer.release();
    });
}
const std::byte* rse_py_data(const Buffer* buffer) noexcept {return buffer->data.data();}
std::size_t rse_py_size(const Buffer* buffer) noexcept {return buffer->data.size();}
void rse_py_free(Buffer* buffer) noexcept {delete buffer;}
}
