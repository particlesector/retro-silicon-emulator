// SPDX-License-Identifier: Apache-2.0
#include <rse/session/policy_playback.hpp>
#include <algorithm>
#include <iostream>

namespace {
std::vector<std::uint8_t> replay_state(std::span<const std::uint8_t> image,
    std::span<const std::uint8_t> recording) {
    using namespace rse;
    nes::Machine replayer(nes::Cartridge::parse(image),{.capture_audio=true});
    const auto target=replayer.load_replay(recording);
    // Split spans at the final target without wrapping on extreme timestamps.
    for(Tick tick=0;tick<target;) {
        tick+=std::min<Tick>(12'345,target-tick);
        replayer.advance_to(tick);
        (void)replayer.take_audio();
    }
    std::cout<<"replay_tick="<<target<<" completed="<<(replayer.inspect_memory(0x12)==1)<<'\n';
    return replayer.snapshot();
}
}
int main(int argc,char** argv) {
    using namespace rse;
    try {
        if(argc==4 && std::string_view(argv[1])=="--replay") {
            const auto image=policy::read_file(argv[2],64*1024);
            const auto recording=policy::read_file(argv[3],12'000'036);
            const auto state=replay_state(image,recording);
            std::cout<<"state_hash="<<std::hex<<rse::state::hash(state)<<'\n';
            return 0;
        }
        if(argc!=5) throw std::invalid_argument("Usage: rse-policy ROM BUNDLE cpu|cuda|auto REPLAY.resl or rse-policy --replay ROM REPLAY.resl");
        const auto image=policy::read_file(argv[1],64*1024);
        auto model=std::make_shared<policy::Model>(argv[2],policy::parse_provider(argv[3]));
        policy::Playback session(model,image);
        constexpr Tick end=757'368+20*714'736;
        constexpr Tick chunk=30'000;
        while(!session.completed() && session.status().tick<end) {
            session.advance_to(std::min(end,session.status().tick+chunk));
            (void)session.take_audio();
        }
        const auto recording=session.replay();
        policy::write_file(argv[4],recording);
        const auto state=replay_state(image,recording);
        if(state!=session.canonical_machine_state())
            throw std::runtime_error("Policy replay state diverged");
        std::cout<<"provider="<<(model->provider()==policy::Provider::cuda?"cuda":"cpu")
                 <<" tick="<<session.status().tick<<" completed="<<session.completed()
                 <<" replay_match=1 state_hash="<<std::hex<<rse::state::hash(state)<<'\n';
        return session.completed() ? 0 : 1;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 2;}
}
