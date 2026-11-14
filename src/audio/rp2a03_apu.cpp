// SPDX-License-Identifier: Apache-2.0
#include <rse/audio/rp2a03_apu.hpp>
#include <stdexcept>

namespace rse::audio {
namespace {
constexpr std::array<std::uint8_t,32> lengths{
    10,254,20,2,40,4,80,6,160,8,60,10,14,12,26,14,
    12,16,24,18,48,20,96,22,192,24,72,26,16,28,32,30};
constexpr std::array<std::uint16_t,16> noise_periods{
    4,8,16,32,64,96,128,160,202,254,380,508,762,1016,2034,4068};
constexpr std::array<std::uint16_t,16> dmc_periods{
    428,380,340,320,286,254,226,214,190,160,142,128,106,84,72,54};
constexpr std::array<std::uint8_t,4> duties{0x02,0x06,0x1e,0xf9};
int target(const PulseState& p,unsigned channel) noexcept {
    const auto delta=p.period>>(p.sweep&7);
    return (p.sweep&8) ? int(p.period)-delta-(channel==0) : int(p.period)+delta;
}
bool muted(const PulseState& p,unsigned channel) noexcept {
    return p.period<8 || target(p,channel)>0x7ff;
}
}
void Envelope::clock() noexcept {
    if (start) { start=false; decay=15; divider=control&15; }
    else if (divider) --divider;
    else { divider=control&15; if (decay) --decay; else if (control&0x20) decay=15; }
}
std::uint8_t Envelope::output() const noexcept { return (control&0x10) ? control&15 : decay; }
void Rp2a03Apu::quarter() noexcept {
    ++state_.quarters;
    for (auto& p:state_.pulse) p.envelope.clock();
    state_.noise.envelope.clock();
    auto& t=state_.triangle;
    if (t.reload) t.linear=t.control&0x7f;
    else if (t.linear) --t.linear;
    if (!(t.control&0x80)) t.reload=false;
}
void Rp2a03Apu::half() noexcept {
    ++state_.halves;
    for (unsigned i=0;i<2;++i) {
        auto& p=state_.pulse[i];
        if (p.length && !(p.envelope.control&0x20)) --p.length;
        if (!p.sweep_divider && (p.sweep&0x80) && (p.sweep&7) && !muted(p,i))
            p.period=static_cast<std::uint16_t>(target(p,i));
        if (!p.sweep_divider || p.sweep_reload) { p.sweep_divider=(p.sweep>>4)&7; p.sweep_reload=false; }
        else --p.sweep_divider;
    }
    if (state_.triangle.length && !(state_.triangle.control&0x80)) --state_.triangle.length;
    if (state_.noise.length && !(state_.noise.envelope.control&0x20)) --state_.noise.length;
}
void Rp2a03Apu::restart_sample() noexcept {
    state_.dmc.address=static_cast<std::uint16_t>(0xc000+64*state_.dmc.address_load);
    state_.dmc.remaining=static_cast<std::uint16_t>(16*state_.dmc.length_load+1);
}
void Rp2a03Apu::request_sample(bool reload) noexcept {
    if (state_.dmc.remaining && !state_.dmc.buffer_full && !request_at_) {
        request_reload_=reload;
        request_at_=state_.cycles+(reload ? 1 : (get_ ? 4 : 3));
    }
}
std::optional<std::uint16_t> Rp2a03Apu::dma_request() const noexcept {
    if (!request_at_ || state_.cycles<*request_at_) return std::nullopt;
    // A request first halts on its designated phase; delayed halts may use either.
    if (state_.cycles==*request_at_ && get_==request_reload_) return std::nullopt;
    return state_.dmc.address;
}
void Rp2a03Apu::accept_dma(std::uint8_t data) {
    if (!request_at_ || !state_.dmc.remaining || state_.dmc.buffer_full)
        throw std::logic_error("unexpected DMC DMA data");
    auto& d=state_.dmc;
    request_at_.reset(); d.buffer=data; d.buffer_full=true;
    d.address=d.address==0xffff ? 0x8000 : static_cast<std::uint16_t>(d.address+1);
    if (!--d.remaining) {
        if (d.control&0x40) restart_sample();
        else if (d.control&0x80) d.irq=true;
    }
}
void Rp2a03Apu::clock(bool get_phase) {
    get_=get_phase; ++state_.cycles;
    ++state_.frame_cycle;
    const auto f=state_.frame_cycle;
    if (f==7457 || f==14913 || f==22371 || (state_.five_step ? f==37281 : f==29829)) {
        quarter(); if (f==14913 || f>22371) half();
    }
    if (!state_.five_step && !state_.inhibit && f>=29828 && f<=29830) state_.frame_irq=true;
    if (f==(state_.five_step ? 37282u : 29830u)) state_.frame_cycle=0;
    if (reset_delay_ && !--reset_delay_) {
        state_.five_step=pending_five_; state_.frame_cycle=0;
        if (state_.five_step) { quarter(); half(); }
    }
    auto& t=state_.triangle;
    if (t.timer) --t.timer;
    else { t.timer=t.period; if (t.length && t.linear) t.phase=(t.phase+1)&31; }
    if (get_) {
        for (auto& p:state_.pulse) {
            if (p.timer) --p.timer;
            else { p.timer=p.period; p.phase=(p.phase+1)&7; }
        }
    }
    auto& n=state_.noise;
    if (n.timer) --n.timer;
    else {
        n.timer=noise_periods[n.period]-1;
        const auto feedback=(n.shift^(n.shift>>(n.mode?6:1)))&1;
        n.shift=static_cast<std::uint16_t>((n.shift>>1)|(feedback<<14));
    }
    auto& d=state_.dmc;
    if (d.timer) --d.timer;
    else {
        d.timer=dmc_periods[d.control&15]-1;
        if (!d.silence) {
            if ((d.shift&1) && d.output<=125) d.output+=2;
            else if (!(d.shift&1) && d.output>=2) d.output-=2;
        }
        d.shift>>=1;
        if (!--d.bits) {
            d.bits=8; d.silence=!d.buffer_full;
            if (d.buffer_full) { d.shift=d.buffer; d.buffer_full=false; request_sample(true); }
        }
    }
}
void Rp2a03Apu::write(std::uint8_t reg,std::uint8_t value) {
    if (reg<=7) {
        auto& p=state_.pulse[reg/4];
        switch (reg&3) {
        case 0:p.envelope.control=value;break;
        case 1:p.sweep=value;p.sweep_reload=true;break;
        case 2:p.period=(p.period&0x700)|value;break;
        case 3:
            p.period=(p.period&255)|((value&7)<<8); p.phase=0;
            if (p.enabled) p.length=lengths[value>>3];
            p.envelope.start=true;break;
        }
    } else switch(reg) {
    case 8:state_.triangle.control=value;break;
    case 10:state_.triangle.period=(state_.triangle.period&0x700)|value;break;
    case 11:
        state_.triangle.period=(state_.triangle.period&255)|((value&7)<<8);
        if (state_.triangle.enabled) state_.triangle.length=lengths[value>>3];
        state_.triangle.reload=true;break;
    case 12:state_.noise.envelope.control=value;break;
    case 14:state_.noise.period=value&15;state_.noise.mode=value&0x80;break;
    case 15:
        if (state_.noise.enabled) state_.noise.length=lengths[value>>3];
        state_.noise.envelope.start=true;break;
    case 16:state_.dmc.control=value;if (!(value&0x80)) state_.dmc.irq=false;break;
    case 17:state_.dmc.output=value&0x7f;break;
    case 18:state_.dmc.address_load=value;break;
    case 19:state_.dmc.length_load=value;break;
    case 21:
        for (unsigned i=0;i<2;++i) {
            state_.pulse[i].enabled=value&(1<<i); if (!state_.pulse[i].enabled) state_.pulse[i].length=0;
        }
        state_.triangle.enabled=value&4;if (!state_.triangle.enabled) state_.triangle.length=0;
        state_.noise.enabled=value&8;if (!state_.noise.enabled) state_.noise.length=0;
        state_.dmc.irq=false;
        if (!(value&16)) { state_.dmc.remaining=0; request_at_.reset(); }
        else if (!state_.dmc.remaining) { restart_sample(); request_sample(false); }
        break;
    case 23:
        pending_five_=value&0x80; state_.inhibit=value&0x40;
        if (state_.inhibit) state_.frame_irq=false;
        reset_delay_=get_?3:4;break;
    default:break;
    }
}
std::uint8_t Rp2a03Apu::read_status() noexcept {
    const auto value=std::uint8_t((state_.pulse[0].length?1:0)|(state_.pulse[1].length?2:0)|
        (state_.triangle.length?4:0)|(state_.noise.length?8:0)|(state_.dmc.remaining?16:0)|
        (state_.frame_irq?64:0)|(state_.dmc.irq?128:0));
    state_.frame_irq=false; return value;
}
bool Rp2a03Apu::irq_asserted() const noexcept { return state_.frame_irq || state_.dmc.irq; }
ChannelOutput Rp2a03Apu::output() const noexcept {
    ChannelOutput out;
    auto pulse=[&](unsigned i) -> std::uint8_t {
        const auto& p=state_.pulse[i];
        if (!p.length || muted(p,i) || !(duties[p.envelope.control>>6]&(1<<p.phase))) return 0;
        return p.envelope.output();
    };
    out.pulse1=pulse(0);out.pulse2=pulse(1);
    const auto phase=state_.triangle.phase;
    out.triangle=phase<16 ? 15-phase : phase-16;
    if (state_.noise.length && !(state_.noise.shift&1)) out.noise=state_.noise.envelope.output();
    out.dmc=state_.dmc.output;return out;
}
} // namespace rse::audio
