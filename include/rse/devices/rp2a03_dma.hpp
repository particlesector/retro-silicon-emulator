// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <rse/devices/rp2a03_oam_dma.hpp>
#include <optional>

namespace rse::devices {
enum class DmcKind { none, halt, dummy, align, get };
struct BusAction { DmaAction oam{DmaKind::cpu}; DmcKind dmc{}; std::uint16_t address{}; };
// 2A03 bus arbitration. DMC preparatory slots overlap OAM work; a DMC get
// steals an OAM get, leaving the OAM sequencer waiting for the next get phase.
class Rp2a03Dma {
public:
    void request_oam(std::uint8_t page) { oam_.request(page); }
    [[nodiscard]] BusAction clock(bool writing,std::uint16_t address,bool get,
                                  std::optional<std::uint16_t> request) {
        BusAction action;
        if (!stage_ && request && !writing) {
            held_=address; source_=*request; stage_=1; action.dmc=DmcKind::halt;
        } else if (stage_==1) { stage_=2; action.dmc=DmcKind::dummy; }
        else if (stage_==2) {
            if (get) { stage_=0; action.dmc=DmcKind::get; action.address=source_; return action; }
            action.dmc=DmcKind::align;
        }
        action.oam=oam_.clock(writing,address,get);
        if (action.dmc!=DmcKind::none) action.address=held_;
        return action;
    }
    void accept_oam(std::uint8_t value) { oam_.accept(value); }
    [[nodiscard]] OamDmaState oam_state() const noexcept { return oam_.state(); }
    [[nodiscard]] bool dmc_active() const noexcept { return stage_!=0; }
private:
    Rp2a03OamDma oam_;
    unsigned stage_{};
    std::uint16_t held_{},source_{};
};
} // namespace rse::devices
