// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <rse/systems/nes/session.hpp>
#include <ostream>

namespace rse::cli {
// Compatibility output for existing NES callers; not part of generic status.
inline void write_nes_v1(const session::Session& machine,std::ostream& output) {
    const auto& nes=dynamic_cast<const nes::Session&>(machine);
    const auto state=nes.diagnostics().state();
    output<<"{\"format\":\"rse.run.v1\",\"ticks\":"<<state.tick<<",\"cpu_cycles\":"<<state.cpu_cycles
          <<",\"frames\":"<<state.ppu.frames<<",\"pc\":"<<state.cpu.pc<<"}\n";
}
}
