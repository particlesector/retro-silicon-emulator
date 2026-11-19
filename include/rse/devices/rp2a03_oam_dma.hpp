// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <rse/core/state_codec.hpp>
#include <cstdint>
#include <stdexcept>

namespace rse::devices {
enum class DmaKind { cpu, halt, align, get, put };
struct DmaAction { DmaKind kind; std::uint16_t address{}; std::uint8_t data{}; };
struct OamDmaState {
    bool pending{}, active{};
    std::uint8_t page{}, data{};
    std::uint16_t index{};
    bool operator==(const OamDmaState&) const = default;
};
// 2A03 OAM DMA sequencing, independent of machine memory/scheduler. The caller
// supplies get/put clock phase, executes actions on the mapped bus, and returns
// each get result via accept(). Rp2a03Dma adds DMC contention around this unit.
class Rp2a03OamDma {
public:
    void request(std::uint8_t page) {
        if (state_.active) throw std::logic_error("cannot request OAM DMA while active");
        state_.page = page; state_.pending = true;
    }
    [[nodiscard]] DmaAction clock(bool cpu_writing, std::uint16_t cpu_address, bool get) {
        if (!state_.active) {
            if (!state_.pending || cpu_writing) return {DmaKind::cpu};
            state_.pending = false; state_.active = true; state_.index = 0;
            held_address_ = cpu_address;
            expecting_put_ = false;
            return {DmaKind::halt, held_address_};
        }
        if (expecting_put_) {
            if (get || !data_ready_) throw std::logic_error("invalid OAM DMA put phase/data");
            const DmaAction action{DmaKind::put,0x2004,state_.data};
            data_ready_ = false; expecting_put_ = false;
            if (++state_.index == 256) state_.active = false;
            return action;
        }
        if (!get) return {DmaKind::align,held_address_};
        expecting_put_ = true;
        return {DmaKind::get,static_cast<std::uint16_t>((state_.page << 8) | state_.index)};
    }
    void accept(std::uint8_t data) {
        if (!expecting_put_ || data_ready_) throw std::logic_error("unexpected OAM DMA read result");
        state_.data = data; data_ready_ = true;
    }
    [[nodiscard]] OamDmaState state() const noexcept { return state_; }
    void save_state(rse::state::Writer& out) const {fields(out,*this);}
    void load_state(rse::state::Reader& in) {
        Rp2a03OamDma next;
        fields(in,next);
        rse::state::require(next.state_.index<=256 && (!next.state_.active || next.state_.index<256) &&
            !(next.state_.pending && next.state_.active) &&
            next.expecting_put_==next.data_ready_ && (!next.expecting_put_ || next.state_.active));
        *this=next;
    }
private:
    template<class Archive,class Self>
    static void fields(Archive& ar,Self& s) {
        ar(s.state_.pending,s.state_.active,s.state_.page,s.state_.data,s.state_.index,
            s.held_address_,s.expecting_put_,s.data_ready_);
    }
    OamDmaState state_;
    std::uint16_t held_address_{};
    bool expecting_put_{}, data_ready_{};
};
} // namespace rse::devices
