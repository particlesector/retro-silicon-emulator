// SPDX-License-Identifier: Apache-2.0
#include <rse/session/builtins.hpp>
#include <rse/systems/nes/session.hpp>
#include <rse/systems/nes/palette.hpp>
#include "acceptance_programs.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <tuple>

namespace {
namespace api=rse::session;
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
template<class Error,class Action> void rejects(Action action) {
    try{action();}catch(const Error&){return;}
    throw std::runtime_error("Expected session rejection");
}

// Deliberately unlike NES: a 3x2 RGBA panel, a rational 333 1/3 Hz clock,
// stereo audio and one named action. This is test infrastructure, not a built-in.
class FakeSession final : public api::Session {
public:
    explicit FakeSession(bool video=true,bool audio=true,bool input=true,std::size_t limit=64)
        :info_{"test-panel","Test panel",{1000,3},
            video?std::optional{api::VideoFormat{3,2,{3,2},api::PixelFormat::rgba8}}:std::nullopt,
            audio?std::optional{api::AudioFormat{22050,2,api::SampleFormat::signed16_native,limit}}:std::nullopt,
            input?std::span<const api::InputPort>(ports_):std::span<const api::InputPort>{},video} {}
    const api::SystemInfo& info() const noexcept override {return info_;}
    api::Status status() const noexcept override {return status_;}
    void reset() override {status_={};events_.clear();pcm_.clear();pixels_={};fire_=false;}
    void advance_to(rse::Tick target) override {
        healthy();
        if(target<status_.tick)throw std::invalid_argument("Test rewind");
        while(status_.tick<target) {
            const auto next=status_.tick+1;
            if(next==13){status_.faulted=true;throw std::runtime_error("Test execution fault");}
            if(info_.audio && pcm_.size()+2>info_.audio->max_buffered_samples) {
                status_.faulted=true;throw std::length_error("Test audio overflow");
            }
            while(!events_.empty() && events_.front().first==next) {
                fire_=events_.front().second;events_.erase(events_.begin());
            }
            status_.tick=next;
            if(info_.audio){pcm_.push_back(std::int16_t(next));pcm_.push_back(-std::int16_t(next));}
            if(info_.video && next%5==0) {
                ++status_.video_frames;
                pixels_.fill({std::uint8_t(fire_?255:next),17,33,255});
            }
        }
    }
    void run_frame() override {
        if(!info_.frame_step)return api::Session::run_frame();
        advance_to((status_.tick/5+1)*5);
    }
    void schedule_input(rse::Tick tick,std::string_view port,std::span<const std::string_view> pressed) override {
        if(info_.input_ports.empty())return api::Session::schedule_input(tick,port,pressed);
        healthy();
        if(tick<=status_.tick || port!="panel" || pressed.size()>1 || (!pressed.empty() && pressed[0]!="fire"))
            throw std::invalid_argument("Invalid test input");
        const auto position=std::upper_bound(events_.begin(),events_.end(),tick,
            [](auto time,const auto& event){return time<event.first;});
        events_.insert(position,{tick,!pressed.empty()});
    }
    std::optional<api::VideoFrame> video() const noexcept override {
        if(!info_.video || !status_.video_frames)return std::nullopt;
        return api::VideoFrame{status_.video_frames,3*sizeof(api::Rgba),std::as_bytes(std::span(pixels_))};
    }
    void convert_video(std::span<api::Rgba> output) const override {
        if(!info_.video)return api::Session::convert_video(output);
        if(output.size()!=pixels_.size())throw std::invalid_argument("Test video size");
        if(!status_.video_frames)throw std::logic_error("Test video not published");
        std::copy(pixels_.begin(),pixels_.end(),output.begin());
    }
    std::vector<std::int16_t> take_audio() override {
        if(!info_.audio)return api::Session::take_audio();
        auto result=std::move(pcm_);pcm_.clear();return result;
    }
private:
    void healthy() const {if(status_.faulted)throw std::logic_error("Test reset required");}
    inline static constexpr std::array<std::string_view,1> actions_{"fire"};
    inline static constexpr std::array<api::InputPort,1> ports_{{{"panel",actions_}}};
    api::SystemInfo info_;
    api::Status status_;
    std::array<api::Rgba,6> pixels_{};
    std::vector<std::pair<rse::Tick,bool>> events_;
    std::vector<std::int16_t> pcm_;
    bool fire_{};
};

void fake_contract() {
    FakeSession fake;
    api::Session& session=fake;
    const auto& info=session.info();
    check(info.clock_hz==api::Rational{1000,3} && info.video->width==3 && info.video->height==2,"fake metadata assumptions");
    check(info.video->display_aspect==api::Rational{3,2} && info.audio->channels==2 && info.audio->sample_rate==22050,"fake output metadata");
    check(!session.video(),"fake unpublished video");
    std::array<api::Rgba,6> rgba;
    rejects<std::logic_error>([&]{session.convert_video(rgba);});
    std::string fire="fire";
    const std::array<std::string_view,1> pressed{fire};
    session.schedule_input(5,"panel",pressed);fire="gone";
    session.advance_to(5);
    check(session.video()->pitch_bytes==12 && session.video()->pixels.size()==24,"fake raw video shape");
    session.convert_video(rgba);check(rgba[0]==api::Rgba{255,17,33,255},"fake copied input or conversion");
    const auto audio=session.take_audio();
    check(audio.size()==10 && audio.front()==1 && audio.back()==-5,"fake stereo audio");
    check(session.take_audio().empty() && session.status().tick==5,"audio drain advanced time");
    session.advance_to(5);rejects<std::invalid_argument>([&]{session.advance_to(4);});
    rejects<std::invalid_argument>([&]{session.schedule_input(5,"panel",{});});
    rejects<std::invalid_argument>([&]{session.schedule_input(6,"unknown",{});});
    const std::array<std::string_view,1> bad{"up"};
    rejects<std::invalid_argument>([&]{session.schedule_input(6,"panel",bad);});
    check(session.status().tick==5 && !session.status().faulted,"validation faulted fake");
    session.run_frame();check(session.status().tick==10 && session.status().video_frames==2,"fake frame step");
    rejects<std::runtime_error>([&]{session.advance_to(13);});
    check(session.status().faulted,"fake execution fault missing");
    rejects<std::logic_error>([&]{session.advance_to(session.status().tick);});
    (void)session.take_audio();check(session.status().faulted,"drain cleared fake fault");
    session.reset();check(!session.video() && session.take_audio().empty() && !session.status().faulted,"fake reset");
    check(rgba[0].r==255 && audio.back()==-5,"owning output changed after reset");
    const std::array<std::string_view,1> action{"fire"};
    session.schedule_input(5,"panel",action);session.schedule_input(5,"panel",{});
    session.advance_to(5);session.convert_video(rgba);check(rgba[0].r==5,"equal-tick input ordering");
    FakeSession overflow(true,true,true,2);
    rejects<std::length_error>([&]{overflow.advance_to(2);});check(overflow.status().faulted,"fake overflow not faulted");
    overflow.reset();check(!overflow.status().faulted,"fake overflow reset");
    FakeSession minimal(false,false,false);
    check(!minimal.info().video && !minimal.info().audio && minimal.info().input_ports.empty(),"absent capabilities");
    check(!minimal.video(),"absent video view");
    rejects<api::UnsupportedOperation>([&]{minimal.run_frame();});
    rejects<api::UnsupportedOperation>([&]{minimal.convert_video(rgba);});
    rejects<api::UnsupportedOperation>([&]{(void)minimal.take_audio();});
    rejects<api::UnsupportedOperation>([&]{minimal.schedule_input(1,"panel",{});});
    minimal.advance_to(9);check(minimal.status().tick==9 && minimal.status().video_frames==0,"headless fake advancement");
}

auto ppu_state(const rse::video::PpuState& s) {
    return std::tuple{s.scanline,s.dot,s.v,s.t,s.fine_x,s.control,s.mask,s.status,s.oam_address,
        s.write_toggle,s.odd,s.nmi,s.faulted,s.dots,s.frames};
}
void compare_state(const rse::nes::Machine& direct,const rse::nes::Session& adapter) {
    const auto a=direct.state(),b=adapter.diagnostics().state();
    check(a.tick==b.tick && a.cpu==b.cpu && a.cpu_cycles==b.cpu_cycles && ppu_state(a.ppu)==ppu_state(b.ppu),"adapter CPU/PPU state mismatch");
    check(a.bus_latch==b.bus_latch && a.buttons==b.buttons && a.controller==b.controller && a.dma==b.dma &&
        a.instruction_boundary==b.instruction_boundary && a.irq==b.irq && a.nmi==b.nmi && a.faulted==b.faulted,"adapter wiring state mismatch");
    const auto status=adapter.status();check(status.tick==a.tick && status.video_frames==a.ppu.frames && status.faulted==a.faulted,"common status mismatch");
    for(unsigned address=0;address<2048;++address)
        check(direct.inspect_memory(address)==adapter.diagnostics().inspect_memory(address),"adapter RAM mismatch");
    const auto left=direct.trace(),right=adapter.diagnostics().trace();
    check(left.size()==right.size() && std::equal(left.begin(),left.end(),right.begin()),"adapter trace mismatch");
}
void nes_replay(NesProgram program,rse::nes::Profile profile) {
    profile.capture_audio=true;profile.trace=true;profile.trace_limit=2'000'000;
    const auto cartridge=program.cartridge();
    rse::nes::Machine direct(cartridge,profile);
    rse::nes::Session adapter(cartridge,profile);
    api::Session& session=adapter;
    const std::array<std::string_view,1> a{"a"};
    std::vector<api::Rgba> rgba(256*240);
    for(unsigned pass=0;pass<2;++pass) {
        if(pass){direct.reset();session.reset();}
        check(!session.video() && session.status().tick==0 && session.take_audio().empty(),"NES reset retained outputs");
        for(unsigned frame=0;frame<12;++frame) {
            const bool pressed=frame>=4 && frame<8;
            direct.schedule_input(direct.state().tick+1,pressed?1:0);
            session.schedule_input(session.status().tick+1,"controller-1",pressed?std::span<const std::string_view>(a):std::span<const std::string_view>{});
            (void)direct.run_frame();
            if(pass) {
                const auto target=direct.state().tick;
                for(auto tick=session.status().tick+997;tick<target;tick+=997)session.advance_to(tick);
                session.advance_to(target);
            }else session.run_frame();
            compare_state(direct,adapter);
            const auto video=session.video();
            const auto expected=std::as_bytes(std::span(direct.frame().samples));
            check(video && video->number==direct.frame().number && video->pitch_bytes==512 &&
                video->pixels.size()==expected.size() && std::equal(expected.begin(),expected.end(),video->pixels.begin()),"adapter frame mismatch");
            session.convert_video(rgba);
            for(unsigned pixel=0;pixel<rgba.size();++pixel)
                check(rgba[pixel]==rse::nes::display_color(direct.frame().samples[pixel]),"adapter palette conversion mismatch");
            check(session.take_audio()==direct.take_audio(),"adapter PCM mismatch");
        }
    }
}
void nes_contract(const std::filesystem::path& file) {
    const auto image=rse::acceptance::image(rse::acceptance::av_input_program());
    check(api::builtin_ids().size()==1 && api::builtin_ids()[0]=="nes-ntsc","built-in IDs");
    auto session=api::create("nes-ntsc",image);
    const auto& info=session->info();
    check(info.id=="nes-ntsc" && info.clock_hz==api::Rational{236250000,11},"NES clock metadata");
    check(info.video->width==256 && info.video->height==240 && info.video->display_aspect==api::Rational{4,3} &&
        info.video->pixels==api::PixelFormat::indexed_u16_native && info.frame_step,"NES video metadata");
    check(info.audio->sample_rate==48000 && info.audio->channels==1 && info.audio->max_buffered_samples==48000,"NES audio metadata");
    check(info.input_ports.size()==1 && info.input_ports[0].actions.size()==8,"NES input metadata");
    std::vector<api::Rgba> rgba(256*240);
    rejects<std::logic_error>([&]{session->convert_video(rgba);});
    rejects<std::invalid_argument>([&]{session->convert_video(std::span(rgba).first(1));});
    const std::array<std::string_view,2> duplicate{"a","a"},unknown{"a","fire"};
    rejects<std::invalid_argument>([&]{session->schedule_input(1,"controller-1",duplicate);});
    rejects<std::invalid_argument>([&]{session->schedule_input(1,"controller-1",unknown);});
    rejects<std::invalid_argument>([&]{session->schedule_input(1,"controller-2",{});});
    rejects<std::invalid_argument>([&]{session->schedule_input(0,"controller-1",{});});
    check(!session->status().faulted && session->status().tick==0,"invalid NES input mutated status");
    session->advance_to(0);
    session->advance_to(1);
    auto* nes=dynamic_cast<rse::nes::Session*>(session.get());
    check(nes && nes->diagnostics().state().buttons==0,"invalid input queued a partial state");
    session->reset();
    std::array<std::string_view,8> all{};std::copy(info.input_ports[0].actions.begin(),info.input_ports[0].actions.end(),all.begin());
    session->schedule_input(1,"controller-1",all);session->advance_to(1);
    check(nes->diagnostics().state().buttons==255,"named input mapping");
    rejects<std::invalid_argument>([&]{session->advance_to(0);});check(!session->status().faulted,"rewind faulted NES");
    session->reset();
    session->schedule_input(1,"controller-1",all);session->schedule_input(1,"controller-1",{});session->advance_to(1);
    check(nes->diagnostics().state().buttons==0,"NES equal-time ordering");
    session->reset();session->run_frame();session->convert_video(rgba);
    const auto owned=rgba;session->reset();check(owned==rgba && !session->video(),"owned video/reset");
    {std::ofstream out(file,std::ios::binary);out.write(reinterpret_cast<const char*>(image.data()),image.size());check(bool(out),"session fixture write");}
    auto loaded=api::load("nes-ntsc",file);loaded->run_frame();check(loaded->video().has_value(),"factory file load");
    auto temporary_image=image;
    auto copied=api::create("nes-ntsc",temporary_image);
    std::fill(temporary_image.begin(),temporary_image.end(),0);
    copied->run_frame();
    check(copied->video()->pixels.size()==loaded->video()->pixels.size() &&
        std::equal(copied->video()->pixels.begin(),copied->video()->pixels.end(),loaded->video()->pixels.begin()),"factory retained caller image storage");
    rejects<std::invalid_argument>([&]{(void)api::create("test-panel",image);});
    rejects<std::invalid_argument>([&]{(void)api::load("unknown",file.string()+".missing");});
    rejects<std::runtime_error>([&]{(void)api::load("nes-ntsc",file.string()+".missing");});
    rejects<std::invalid_argument>([&]{(void)api::create("nes-ntsc",{});});
    auto unsupported=image;unsupported[6]=0x10;
    rejects<std::invalid_argument>([&]{(void)api::create("nes-ntsc",unsupported);});
    unsupported=image;unsupported[7]=8;
    rejects<std::invalid_argument>([&]{(void)api::create("nes-ntsc",unsupported);});
    NesProgram bad;bad.emit({0x02});rse::nes::Session fault(bad.cartridge());
    rejects<std::runtime_error>([&]{fault.advance_to(1000);});check(fault.status().faulted,"NES execution fault missing");
    rejects<std::logic_error>([&]{fault.advance_to(fault.status().tick);});fault.reset();check(!fault.status().faulted,"NES fault reset");
    auto program=rse::acceptance::av_input_program();rse::nes::Session overflow(program.cartridge(),{.audio_limit=2});
    rejects<std::length_error>([&]{overflow.advance_to(100000);});check(overflow.status().faulted,"NES audio bound");
    (void)overflow.take_audio();check(overflow.status().faulted,"NES drain cleared fault");overflow.reset();
    check(!overflow.status().faulted && overflow.take_audio().empty(),"NES overflow reset");
    std::uint64_t hash=14695981039346656037ull;
    for(unsigned color=0;color<512;++color) {
        const auto c=rse::nes::display_color(color);
        for(auto byte:{c.r,c.g,c.b,c.a})hash=(hash^byte)*1099511628211ull;
    }
    check(hash==0xeb035541bc4109e5ull,"accepted palette changed during extraction");
}
}
int main(int argc,char** argv) {
    try {
        check(argc==2,"Expected fixture output path");
        fake_contract();nes_contract(argv[1]);
        for(auto [name,program]:rse::acceptance::programs())nes_replay(std::move(program),{});
        nes_replay(rse::acceptance::av_input_program(),{.ppu_phase=3,.cpu_phase=7,.dma_get_on_odd=false});
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
