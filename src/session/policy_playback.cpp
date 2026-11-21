// SPDX-License-Identifier: Apache-2.0
#include <rse/session/policy_playback.hpp>
#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <mutex>

namespace rse::policy {
namespace {
constexpr std::array<std::string_view,8> buttons{"a","b","select","start","up","down","left","right"};
constexpr std::string_view port="controller-1";
constexpr std::array<std::string_view,1> right{"right"};
constexpr std::array<std::string_view,2> jump{"right","a"};
constexpr std::array<std::span<const std::string_view>,3> mappings{{{},right,jump}};
constexpr Tick interval=714'736, warmup=757'368;
constexpr unsigned horizon=20;
constexpr std::size_t model_limit=4*1024*1024;
constexpr char input_name[]="position", output_name[]="logits";
constexpr std::int64_t input_shape[]{1,12};

void initialize_runtime() {
    static std::once_flag initialized;
    std::call_once(initialized,[] {
        const auto* runtime=OrtGetApiBase();
        const auto* api=runtime->GetApi(ORT_API_VERSION);
        if(!api) throw std::runtime_error(std::string("Incompatible ONNX Runtime library: ")+
            runtime->GetVersionString());
        Ort::InitApi(api);
    });
}

unsigned argmax(const std::array<float,3>& values) {
    return static_cast<unsigned>(std::ranges::max_element(values)-values.begin());
}
nes::Cartridge cartridge(const std::shared_ptr<const Model>& model,std::span<const std::uint8_t> image) {
    if(!model || state::hash(image)!=model->rom_hash())
        throw std::invalid_argument("Policy bundle does not match this ROM");
    return nes::Cartridge::parse(image);
}
}
std::vector<std::uint8_t> read_file(const std::filesystem::path& path,std::size_t limit) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    const auto length=file.tellg();
    if(length<0 || static_cast<std::uintmax_t>(length)>limit)
        throw std::invalid_argument("Cannot open file or file exceeds size limit");
    std::vector<std::uint8_t> result(static_cast<std::size_t>(length));
    file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(result.data()),static_cast<std::streamsize>(result.size())))
        throw std::runtime_error("Cannot read policy playback file");
    return result;
}
void write_file(const std::filesystem::path& path,std::span<const std::uint8_t> data) {
    if(path.extension()!=".resl" || std::filesystem::exists(path))
        throw std::invalid_argument("Choose a new .resl replay path");
    std::ofstream file(path,std::ios::binary);
    file.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));
    file.close();
    if(!file) throw std::runtime_error("Cannot write policy replay");
}
Provider parse_provider(std::string_view name) {
    if(name=="cpu") return Provider::cpu;
    if(name=="cuda") return Provider::cuda;
    if(name=="auto") return Provider::automatic;
    throw std::invalid_argument("Expected policy provider cpu, cuda or auto");
}
struct Model::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING,"rse-policy"};
    Ort::Session session{nullptr};
    Provider selected{Provider::cpu};
    std::uint64_t rom{};

    void connect(std::span<const std::uint8_t> model,Provider provider) {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(1);
        if(provider==Provider::cuda) {
            const auto providers=Ort::GetAvailableProviders();
            if(std::ranges::find(providers,"CUDAExecutionProvider")==providers.end())
                throw std::runtime_error("ONNX Runtime CUDA provider is unavailable");
            const auto& api=Ort::GetApi();
            OrtCUDAProviderOptionsV2* cuda{};
            Ort::ThrowOnError(api.CreateCUDAProviderOptions(&cuda));
            const std::unique_ptr<OrtCUDAProviderOptionsV2,decltype(api.ReleaseCUDAProviderOptions)>
                settings(cuda,api.ReleaseCUDAProviderOptions);
            const char* keys[]{"use_tf32"};
            const char* values[]{"0"};
            Ort::ThrowOnError(api.UpdateCUDAProviderOptions(settings.get(),keys,values,1));
            options.AppendExecutionProvider_CUDA_V2(*settings);
            options.AddConfigEntry("session.disable_cpu_ep_fallback","1");
        }
        session=Ort::Session(env,model.data(),model.size(),options);
        selected=provider;
    }
    void validate() const {
        Ort::AllocatorWithDefaultOptions allocator;
        if(session.GetInputCount()!=1 || session.GetOutputCount()!=1)
            throw std::invalid_argument("Unexpected policy tensor count");
        const auto input=session.GetInputNameAllocated(0,allocator);
        const auto output=session.GetOutputNameAllocated(0,allocator);
        const auto in_type=session.GetInputTypeInfo(0);
        const auto out_type=session.GetOutputTypeInfo(0);
        const auto in=in_type.GetTensorTypeAndShapeInfo();
        const auto out=out_type.GetTensorTypeAndShapeInfo();
        if(std::string_view(input.get())!=input_name || std::string_view(output.get())!=output_name ||
            in.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
            out.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
            in.GetShape()!=std::vector<std::int64_t>{1,12} ||
            out.GetShape()!=std::vector<std::int64_t>{1,3})
            throw std::invalid_argument("Policy tensors do not match Signal Run v1");
    }
    std::array<float,3> infer(unsigned position) {
        if(position>=12) throw std::out_of_range("Unsupported Signal Run position");
        std::array<float,12> input{};
        input[position]=1;
        auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
        auto tensor=Ort::Value::CreateTensor<float>(memory,input.data(),input.size(),input_shape,2);
        const char* inputs[]{input_name};
        const char* outputs[]{output_name};
        const auto result=session.Run(Ort::RunOptions{nullptr},inputs,&tensor,1,outputs,1);
        const auto* values=result.front().GetTensorData<float>();
        std::array<float,3> logits{values[0],values[1],values[2]};
        if(!std::ranges::all_of(logits,[](float value){return std::isfinite(value);}))
            throw std::runtime_error("Policy output contains nonfinite logits");
        return logits;
    }
};
Model::Model(const std::filesystem::path& bundle,Provider provider) {
    initialize_runtime();
    impl_=std::make_unique<Impl>();
    const auto bytes=read_file(bundle,model_limit+256);
    state::Reader reader(bytes);
    std::uint32_t magic,length;
    std::uint16_t version,task;
    std::uint64_t expected;
    reader(magic,version,task,impl_->rom,expected,length);
    state::require(magic==0x50455352 && version==1 && task==1 && length>0 && length<=model_limit);
    std::array<std::array<float,3>,12> reference{};
    for(auto& row:reference)
        for(auto& value:row) {
            std::uint32_t bits;
            reader(bits);
            value=std::bit_cast<float>(bits);
            state::require(std::isfinite(value));
        }
    state::require(reader.remaining()==length);
    const auto model=std::span(bytes).last(length);
    state::require(state::hash(model)==expected);
    const auto connect=[&](Provider candidate) {
        impl_->connect(model,candidate);
        impl_->validate();
        for(unsigned position=0;position<reference.size();++position) {
            const auto actual=impl_->infer(position);
            const auto& target=reference[position];
            for(unsigned i=0;i<actual.size();++i)
                state::require(std::abs(actual[i]-target[i])<=1e-5f+1e-4f*std::abs(target[i]));
            state::require(argmax(actual)==argmax(target));
        }
    };
    if(provider==Provider::automatic) {
        try {connect(Provider::cuda);}
        catch(const std::exception&) {connect(Provider::cpu);}
    } else connect(provider);
}
Model::~Model()=default;
Provider Model::provider() const noexcept {return impl_->selected;}
std::uint64_t Model::rom_hash() const noexcept {return impl_->rom;}
std::array<float,3> Model::logits(unsigned position) const {return impl_->infer(position);}
unsigned Model::choose(unsigned position) const {return argmax(logits(position));}

Playback::Playback(std::shared_ptr<const Model> model,std::span<const std::uint8_t> image)
    :model_(std::move(model)),machine_(cartridge(model_,image)),info_(machine_.info()) {
    info_.frame_step=false;
}
session::Status Playback::status() const noexcept {
    auto status=machine_.status();
    status.faulted=status.faulted || faulted_;
    return status;
}
void Playback::reset() {
    machine_.reset();
    actions_.clear();
    next_decision_=warmup;
    steps_=0;
    manual_=stopped_=faulted_=false;
}
void Playback::apply(Tick tick,std::string_view key,std::span<const std::string_view> pressed) {
    if(actions_.size()==1'000'000) throw std::length_error("Policy replay action limit exceeded");
    if(actions_.size()==actions_.capacity()) actions_.reserve(std::min<std::size_t>(1'000'000,2*actions_.size()+16));
    machine_.schedule_input(tick,key,pressed);
    std::uint8_t mask{};
    for(auto button:pressed)
        mask|=static_cast<std::uint8_t>(1u<<(std::ranges::find(buttons,button)-buttons.begin()));
    actions_.push_back({tick,nes::ReplayAction::Kind::buttons,mask});
}
void Playback::schedule_input(Tick tick,std::string_view key,std::span<const std::string_view> pressed) {
    const auto current=status();
    if(current.faulted) throw std::logic_error("Policy playback requires reset");
    if(tick<=current.tick || tick-current.tick!=1 || key!=port)
        throw std::invalid_argument("Policy playback expects next-tick controller input");
    if(!manual_ && pressed.empty()) return;
    apply(tick,key,pressed);
    manual_=true;
}
void Playback::advance_to(Tick target) {
    const auto current=status();
    if(current.faulted) throw std::logic_error("Policy playback requires reset");
    if(target<current.tick) throw std::invalid_argument("Cannot rewind policy playback");
    try {
        while(!manual_ && !stopped_ && next_decision_<target) {
            machine_.advance_to(next_decision_);
            const auto position=machine_.diagnostics().inspect_memory(0x10).value();
            stopped_=completed() || steps_==horizon;
            apply(next_decision_+1,port,mappings[stopped_ ? 0 : model_->choose(position)]);
            next_decision_+=interval;
            ++steps_;
        }
        machine_.advance_to(target);
    } catch(...) {faulted_=true; throw;}
}
bool Playback::completed() const noexcept {return machine_.diagnostics().inspect_memory(0x12)==1;}
std::uint8_t Playback::active_buttons() const noexcept {
    return machine_.diagnostics().state().buttons;
}
std::vector<std::uint8_t> Playback::replay() const {
    const auto tick=status().tick;
    const auto end=std::ranges::upper_bound(actions_,tick,{},&nes::ReplayAction::tick);
    return machine_.diagnostics().encode_replay(tick,std::span(actions_.begin(),end));
}
}
