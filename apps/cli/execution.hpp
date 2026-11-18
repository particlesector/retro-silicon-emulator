// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <rse/session/session.hpp>
#include <rse/session/time.hpp>
#include <algorithm>

namespace rse::cli {
inline void execute(session::Session& machine,bool frames,std::uint64_t count) {
    if(frames) {
        for(std::uint64_t i=0;i<count;++i) {
            machine.run_frame();
            if(machine.info().audio)(void)machine.take_audio();
        }
    } else {
        // Drain periodically even when the CLI discards audio. Respect the
        // adapter's queue bound without accumulating a run's entire PCM.
        const auto step=session::ticks_for_us(machine.info().clock_hz,1000);
        while(machine.status().tick<count) {
            const auto now=machine.status().tick;
            machine.advance_to(now+std::min(step,count-now));
            if(machine.info().audio)(void)machine.take_audio();
        }
    }
}
}
