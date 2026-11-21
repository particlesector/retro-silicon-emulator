// SPDX-License-Identifier: Apache-2.0
#include "trainer.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace rse::desktop {
namespace {
std::string utf8(const std::filesystem::path& path) {
    const auto value=path.u8string();
    return {value.begin(),value.end()};
}
constexpr std::size_t frame_width=256, frame_height=240, rgb_size=frame_width*frame_height*3;
}
struct Trainer::Run {
    explicit Run(const TrainerConfig& config):config_(config) {}
    ~Run();
    void start(const std::filesystem::path&,const std::filesystem::path&,std::string_view,
        const std::filesystem::path&);
    void stop();
    bool poll();
    [[nodiscard]] bool running() const noexcept {return bool(process_);}
    void telemetry();
    [[nodiscard]] Snapshot snapshot() const {return {status_,metrics_,preview_,checkpoint_,running()};}
    const TrainerConfig& config_;
    std::unique_ptr<SDL_Process,decltype(&SDL_DestroyProcess)> process_{nullptr,SDL_DestroyProcess};
    std::filesystem::path directory_,stop_file_,checkpoint_;
    std::optional<std::filesystem::file_time_type> preview_stamp_;
    std::string status_{"Ready"};
    TrainerMetrics metrics_;
    Frame preview_;
    std::uint64_t next_poll_{};
    bool stopping_{};
};
Trainer::Trainer(TrainerConfig config):config_(std::move(config)) {
    if(config_.python.empty() || config_.script.empty() || config_.bridge.empty() || config_.output.empty() ||
        (config_.device!="cpu" && config_.device!="cuda" && config_.device!="auto") || !config_.steps)
        throw std::invalid_argument("Incomplete trainer configuration");
}
Trainer::~Trainer()=default;
void Trainer::start(const std::filesystem::path& rom,const std::filesystem::path& profile,
    std::string_view observation,const std::filesystem::path& resume) {
    if(running()) throw std::logic_error("A trainer is already running");
    if(rom.empty() || profile.empty()) throw std::invalid_argument("Select a ROM and task profile first");
    stop_requested_.store(false);
    worker_=std::jthread([this,rom,profile,mode=std::string(observation),resume](std::stop_token stop) {
        try {
            Run run(config_);
            run.start(rom,profile,mode,resume);
            while(run.running() && !stop.stop_requested()) {
                if(stop_requested_.load()) run.stop();
                if(run.poll()) publish(run.snapshot());
                if(run.running()) SDL_Delay(25);
            }
        } catch(const std::exception& e) {
            publish({e.what(),{},{},{},false});
        }
    });
    status_="Starting trainer";
    metrics_={};preview_={};
    running_=true;
}
void Trainer::stop() {
    if(running()) {
        stop_requested_.store(true);
        status_="Stopping after PPO update";
    }
}
void Trainer::publish(Snapshot snapshot) {
    std::lock_guard lock(pending_mutex_);
    pending_=std::move(snapshot);
}
bool Trainer::poll() {
    std::optional<Snapshot> next;
    {
        std::lock_guard lock(pending_mutex_);
        next.swap(pending_);
    }
    if(!next) return false;
    if(!next->checkpoint.empty()) checkpoint_=std::move(next->checkpoint);
    running_=next->running;
    status_=running_ && stop_requested_.load() ? "Stopping after PPO update" : std::move(next->status);
    metrics_=next->metrics;
    preview_=std::move(next->preview);
    return true;
}
Trainer::Run::~Run() {
    if(!running()) return;
    try {stop();} catch(...) {}
    const auto deadline=SDL_GetTicks()+8'000;
    while(!SDL_WaitProcess(process_.get(),false,nullptr) && SDL_GetTicks()<deadline) SDL_Delay(20);
    if(!SDL_WaitProcess(process_.get(),false,nullptr) && SDL_KillProcess(process_.get(),true))
        (void)SDL_WaitProcess(process_.get(),true,nullptr);
}
void Trainer::Run::start(const std::filesystem::path& rom,const std::filesystem::path& profile,
    std::string_view observation,const std::filesystem::path& resume) {
    if(running()) throw std::logic_error("A trainer is already running");
    if(rom.empty() || profile.empty()) throw std::invalid_argument("Select a ROM and task profile first");
    std::filesystem::create_directories(config_.output);
    const auto nonce=std::chrono::system_clock::now().time_since_epoch().count();
    const auto run=config_.output/("run-"+std::to_string(nonce));
    if(!std::filesystem::create_directory(run)) throw std::runtime_error("Trainer output already exists");
    auto directory=run/"output";
    auto stop_file=run/"stop.request";
    std::vector<std::string> command{utf8(config_.python),"-B","-u",utf8(config_.script),
        "--bridge",utf8(config_.bridge),"--rom",utf8(rom),"--profile",utf8(profile),
        "--out",utf8(directory),"--device",config_.device,"--steps",std::to_string(config_.steps),
        "--stop-file",utf8(stop_file),"--live"};
    if(!observation.empty()) {
        command.emplace_back("--observation");
        command.emplace_back(observation);
    }
    if(!resume.empty()) {
        command.emplace_back("--resume");
        command.push_back(utf8(resume));
    }
    std::vector<const char*> args;
    args.reserve(command.size()+1);
    for(const auto& item:command) args.push_back(item.c_str());
    args.push_back(nullptr);
    process_.reset(SDL_CreateProcess(args.data(),false));
    if(!process_) throw std::runtime_error(std::string("Cannot launch trainer: ")+SDL_GetError());
    directory_=std::move(directory);
    stop_file_=std::move(stop_file);
    metrics_={};preview_={};preview_stamp_.reset();
    next_poll_=0;
    stopping_=false;
    status_="Training";
}
void Trainer::Run::stop() {
    if(!running() || stopping_) return;
    std::ofstream request(stop_file_,std::ios::out);
    if(!request) throw std::runtime_error("Cannot write trainer stop request");
    stopping_=true;
    status_="Stopping after PPO update";
}
bool Trainer::Run::poll() {
    if(!running() || SDL_GetTicks()<next_poll_) return false;
    next_poll_=SDL_GetTicks()+250;
    telemetry();
    int exit{};
    if(SDL_WaitProcess(process_.get(),false,&exit)) {
        process_.reset();
        if(exit==0) {
            const auto candidate=directory_/"checkpoint.zip";
            std::error_code issue;
            if(std::filesystem::is_regular_file(candidate,issue)) checkpoint_=candidate;
            status_=stopping_ ? "Stopped at rollout boundary" : "Training completed";
        } else status_="Trainer disconnected (exit "+std::to_string(exit)+"). See console.";
        stopping_=false;
    }
    return true;
}
void Trainer::Run::telemetry() {
    std::array<char,256> text{};
    std::ifstream stats(directory_/"live.txt",std::ios::binary);
    stats.read(text.data(),text.size());
    if(stats.gcount()>0 && stats.gcount()<static_cast<std::streamsize>(text.size())) {
        std::istringstream fields(std::string(text.data(),static_cast<std::size_t>(stats.gcount())));
        fields.imbue(std::locale::classic());
        std::string magic;
        TrainerMetrics next;
        int has_reward,has_loss,action;
        double reward,loss;
        if(fields>>magic>>next.steps>>next.rate>>has_reward>>reward>>has_loss>>loss>>action &&
            magic=="RSE19" && next.steps>=metrics_.steps && next.rate>=0 && std::isfinite(next.rate) &&
            (has_reward==0 || has_reward==1) && (has_loss==0 || has_loss==1) &&
            action>=-1 && std::isfinite(reward) && std::isfinite(loss)) {
            if(has_reward) next.reward=reward;
            if(has_loss) next.loss=loss;
            if(action>=0) next.action=static_cast<unsigned>(action);
            metrics_=next;
        }
    }
    const auto path=directory_/"preview.rgb";
    std::error_code issue;
    const auto stamp=std::filesystem::last_write_time(path,issue);
    if(issue || preview_stamp_==stamp) return;
    std::ifstream frame(path,std::ios::binary|std::ios::ate);
    if(frame.tellg()!=static_cast<std::streamoff>(rgb_size)) return;
    std::vector<std::uint8_t> bytes(rgb_size);
    frame.seekg(0);
    if(!frame.read(reinterpret_cast<char*>(bytes.data()),bytes.size())) return;
    preview_.format={frame_width,frame_height,{4,3},session::PixelFormat::rgba8};
    preview_.pixels.resize(frame_width*frame_height);
    for(std::size_t i=0;i<preview_.pixels.size();++i)
        preview_.pixels[i]={bytes[3*i],bytes[3*i+1],bytes[3*i+2],255};
    ++preview_.number;
    preview_stamp_=stamp;
}
}
