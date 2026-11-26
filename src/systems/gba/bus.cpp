// SPDX-License-Identifier: Apache-2.0
#include <rse/systems/gba/bus.hpp>
#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace rse::gba {
namespace {
constexpr unsigned dma_start = 0xb0, dma_end = 0xe0;
constexpr unsigned timer_start = 0x100, timer_end = 0x110;
constexpr std::uint32_t entry = 0x08000000;

std::uint32_t read_le(std::span<const std::uint8_t> bytes, unsigned offset, unsigned width) {
    std::uint32_t value{};
    for (unsigned i = 0; i < width; ++i) value |= std::uint32_t(bytes[offset + i]) << (8 * i);
    return value;
}

void write_le(std::span<std::uint8_t> bytes, unsigned offset, unsigned width, std::uint32_t value) {
    for (unsigned i = 0; i < width; ++i) bytes[offset + i] = std::uint8_t(value >> (8 * i));
}

unsigned vram_offset(unsigned address) {
    const unsigned offset = address & 0x1ffff;
    return offset < 0x18000 ? offset : offset - 0x8000;
}

std::uint32_t next_dma_address(std::uint32_t address, unsigned mode, unsigned width) {
    if (mode == 1) return address - width;
    if (mode == 2) return address;
    return address + width;
}
}

Bus::Bus(std::span<const std::uint8_t> bios, std::span<const std::uint8_t> cartridge) {
    if (bios.empty()) throw std::invalid_argument("GBA BIOS is required (supply a 16384-byte BIOS)");
    if (bios.size() != bios_size) throw std::invalid_argument("GBA BIOS must be exactly 16384 bytes");
    if (cartridge.size() < cartridge_header_size)
        throw std::invalid_argument("Truncated GBA cartridge: the header requires at least 192 bytes");
    if (cartridge.size() > cartridge_max_size)
        throw std::invalid_argument("GBA cartridge exceeds the 32 MiB Game Pak ROM limit");
    std::ranges::copy(bios, bios_.begin());
    rom_.assign(cartridge.begin(), cartridge.end());
    reset();
}

void Bus::reset() noexcept {
    ewram_.fill(0);
    iwram_.fill(0);
    palette_.fill(0);
    vram_.fill(0);
    oam_.fill(0);
    io_.fill(0);
    io_[0] = 0x0080; // Forced blank after power-on.
    dma_.fill({});
    dma_owner_.reset();
    dma_transfer_.reset();
    display_.reset();
    timers_ = {};
    input_.clear();
    pressed_ = keycnt_ = 0;
    key_match_ = false;
    complete_cpu_attempt();
    branch();
    entry_fetch_.reset();
    ticks_ = 0;
    instruction_address_ = bios_latch_ = opcode_latch_ = 0;
    line_ = dot_ = 0;
    ie_ = if_ = waitcnt_ = 0;
    ime_ = halted_ = false;
}

bool Bus::Sequence::follows(std::uint32_t next, Access access) const noexcept {
    return kind == access && next == address + width && (next >> 25) == (address >> 25);
}

unsigned Bus::access_ticks(std::uint32_t address, unsigned width, bool sequential) const noexcept {
    const unsigned region = address >> 24;
    const unsigned beats = width == 4 ? 2 : 1;
    if (region == 2) return 3 * beats;
    if (region == 5 || region == 6) return beats;
    if (region >= 8 && region <= 13) {
        constexpr unsigned nonseq_waits[] = {4, 3, 2, 8};
        const unsigned bank = (region - 8) / 2;
        const unsigned shift = 2 + 3 * bank;
        const unsigned seq = (waitcnt_ & (4u << shift)) ? 2 : 1 + (2u << bank);
        const unsigned nonseq = 1 + nonseq_waits[(waitcnt_ >> shift) & 3];
        const bool boundary = (address & 0x1fffe) == 0;
        return (sequential && !boundary ? seq : nonseq) + (beats - 1) * seq;
    }
    return 1;
}

unsigned Bus::ticks_to_lcd_event() const noexcept {
    return (dot_ < hblank_tick ? hblank_tick : line_ticks) - dot_;
}

std::uint64_t Bus::ticks_to_event() const noexcept {
    std::uint64_t next = ticks_to_lcd_event();
    if (timers_.next_overflow() != Timers::never) next = std::min(next, timers_.next_overflow() - ticks_);
    if (!input_.empty()) next = std::min(next, input_.begin()->first - ticks_);
    return next;
}

void Bus::emit(TimingKind kind, unsigned value) const noexcept {
    if (sink_) sink_(sink_context_, {ticks_, kind, value});
}

void Bus::handle_lcd_event() {
    const auto stat = io_[2];
    if (dot_ == hblank_tick) {
        if (line_ < screen_height) {
            display_.draw(line_, io_, vram_, palette_, oam_);
            trigger_dma(2);
        }
        if (stat & 0x10) if_ |= 2;
        return;
    }
    dot_ = 0;
    line_ = (line_ + 1) % frame_lines;
    if (line_ == screen_height) {
        emit(TimingKind::vblank_enter);
        display_.publish();
        emit(TimingKind::frame);
        if (stat & 8) if_ |= 1;
        trigger_dma(1);
    } else if (line_ == frame_lines - 1) {
        emit(TimingKind::vblank_exit);
    } else if (line_ == 0) {
        display_.begin_frame(io_);
    }
    if (line_ == (stat >> 8) && (stat & 0x20)) if_ |= 4;
}

void Bus::advance(unsigned ticks) {
    auto next = ticks_to_event();
    while (ticks >= next) {
        const auto lcd = ticks_to_lcd_event();
        const auto span = unsigned(next);
        ticks -= span;
        ticks_ += span;
        dot_ += span;
        // Peripheral edges precede a bus access completing on the same tick.
        if (span == lcd) handle_lcd_event();
        if (timers_.next_overflow() != Timers::never && timers_.next_overflow() == ticks_) {
            const auto wrapped = timers_.overflow(ticks_);
            for (unsigned timer = 0; timer < 4; ++timer) {
                if (!(wrapped & (1u << timer))) continue;
                emit(TimingKind::timer_overflow, timer);
                if (timers_.irq_enabled(timer)) if_ |= std::uint16_t(8u << timer);
            }
        }
        while (!input_.empty() && input_.begin()->first == ticks_) {
            pressed_ = input_.begin()->second;
            input_.erase(input_.begin());
            update_key_irq();
        }
        next = ticks_to_event();
    }
    ticks_ += ticks;
    dot_ += ticks;
    if (wake()) halted_ = false;
}

void Bus::idle_to_lcd_event() { advance(ticks_to_lcd_event()); }
void Bus::idle(std::uint64_t limit) { advance(unsigned(std::min(ticks_to_event(), limit - ticks_))); }

void Bus::schedule_keys(std::uint64_t tick, std::uint16_t pressed) {
    if (tick <= ticks_ || pressed > 0x3ff) throw std::invalid_argument("Invalid GBA keypad event");
    input_.emplace(tick, pressed);
}

void Bus::update_key_irq() noexcept {
    const auto selected = keycnt_ & 0x3ff;
    const auto active = pressed_ & selected;
    const bool matches = (keycnt_ & 0x4000) && selected &&
                         ((keycnt_ & 0x8000) ? active == selected : active != 0);
    if (matches && !key_match_) if_ |= 0x1000;
    key_match_ = matches;
    if (wake()) halted_ = false;
}

bool Bus::consume(unsigned& clocks, std::uint64_t limit) {
    const auto span = unsigned(std::min<std::uint64_t>(clocks, limit - ticks_));
    advance(span);
    clocks -= span;
    return clocks == 0;
}

void Bus::begin_cpu_attempt(std::uint64_t limit) noexcept {
    cpu_cursor_ = 0;
    cpu_limit_ = limit;
    cpu_bounded_ = limit != Timers::never || cpu_count_ != 0 || cpu_clocks_ != 0;
}

void Bus::complete_cpu_attempt() noexcept {
    cpu_count_ = cpu_cursor_ = cpu_clocks_ = 0;
    cpu_limit_ = Timers::never;
    cpu_bounded_ = false;
}

void Bus::advance_cpu_access(unsigned clocks) {
    if (!cpu_bounded_) {
        advance(clocks);
        return;
    }
    if (cpu_clocks_ == 0) cpu_clocks_ = clocks;
    if (!consume(cpu_clocks_, cpu_limit_)) throw Arm7Bus::Stall{};
}

std::uint16_t Bus::read_io(unsigned address) const {
    switch (address) {
    case 0x004:
        return io_[2] | (line_ >= screen_height && line_ < frame_lines - 1 ? 1 : 0) |
            (dot_ >= hblank_tick ? 2 : 0) | (line_ == (io_[2] >> 8) ? 4 : 0);
    case 0x006: return std::uint16_t(line_);
    case 0x130: return 0x03ff ^ pressed_;
    case 0x132: return keycnt_;
    case 0x200: return ie_;
    case 0x202: return if_;
    case 0x204: return waitcnt_;
    case 0x208: return ime_;
    default:
        if (address >= dma_start && address < dma_end) {
            const unsigned reg = address - dma_start;
            return reg % 12 == 10 ? dma_[reg / 12].control : 0;
        }
        if (address >= timer_start && address < timer_end) return timers_.read(address - timer_start, ticks_);
        return io_[address / 2];
    }
}

void Bus::write_io(unsigned address, std::uint16_t value, std::uint16_t mask) {
    const auto merge = [=](std::uint16_t old) { return (old & ~mask) | (value & mask); };
    switch (address) {
    case 0x004: {
        const bool was_match = line_ == (io_[2] >> 8);
        io_[2] = merge(io_[2]) & 0xff38;
        if (!was_match && line_ == (io_[2] >> 8) && (io_[2] & 0x20)) if_ |= 4;
        break;
    }
    case 0x006: case 0x130: break;
    case 0x132:
        keycnt_ = merge(keycnt_) & 0xc3ff;
        update_key_irq();
        break;
    case 0x200: ie_ = merge(ie_) & 0x3fff; break;
    case 0x202: {
        const auto cleared = if_ & value & mask;
        if_ &= ~cleared;
        if (cleared) emit(TimingKind::irq_acknowledge, cleared);
        break;
    }
    case 0x204: waitcnt_ = merge(waitcnt_) & 0x5fff; break;
    case 0x208: ime_ = (merge(ime_) & 1) != 0; break;
    case 0x300:
        if (instruction_address_ < bios_size) {
            io_[0x180] = merge(io_[0x180]) & 1;
            if ((mask & 0xff00) && !(value & 0x8000)) halted_ = true;
        }
        break;
    default:
        if (address >= dma_start && address < dma_end) write_dma(address, value, mask);
        else if (address >= timer_start && address < timer_end) timers_.write(address - timer_start, value, mask, ticks_);
        else {
            io_[address / 2] = merge(io_[address / 2]);
            if ((address >= 0x28 && address <= 0x2e) || (address >= 0x38 && address <= 0x3e))
                display_.write_reference(address, io_);
        }
        break;
    }
    if (wake()) halted_ = false;
}

std::uint32_t Bus::read_data(std::uint32_t address, unsigned width, bool bios_access) const {
    const auto lane = [=](std::uint32_t word) {
        return (word >> (8 * (address & (4 - width)))) & (0xffffffffu >> (8 * (4 - width)));
    };
    switch (address >> 24) {
    case 0:
        if (address < bios_size) return bios_access ? read_le(bios_, address, width) : lane(bios_latch_);
        break;
    case 2: return read_le(ewram_, address & 0x3ffff, width);
    case 3: return read_le(iwram_, address & 0x7fff, width);
    case 4:
        if (address < 0x04000400) {
            const unsigned offset = address & 0x3fe;
            const std::uint32_t low = read_io(offset);
            if (width == 4) return low | (std::uint32_t(read_io(offset + 2)) << 16);
            if (width == 2) return low;
            return (low >> (8 * (address & 1))) & 0xff;
        }
        break;
    case 5: return read_le(palette_, address & 0x3ff, width);
    case 6: return read_le(vram_, vram_offset(address), width);
    case 7: return read_le(oam_, address & 0x3ff, width);
    case 8: case 9: case 10: case 11: case 12: case 13: {
        const unsigned offset = address & 0x1ffffff;
        if (offset + width <= rom_.size()) return read_le(rom_, offset, width);
        std::uint32_t value{};
        for (unsigned i = 0; i < width; ++i) {
            const unsigned byte = offset + i;
            const auto data = byte < rom_.size() ? rom_[byte] : (byte >> (1 + 8 * (byte & 1))) & 0xff;
            value |= std::uint32_t(data) << (8 * i);
        }
        return value;
    }
    }
    return lane(opcode_latch_);
}

void Bus::write_data(std::uint32_t address, unsigned width, std::uint32_t value) {
    switch (address >> 24) {
    case 2: write_le(ewram_, address & 0x3ffff, width, value); break;
    case 3: write_le(iwram_, address & 0x7fff, width, value); break;
    case 4:
        if (address >= 0x04000400) break;
        if (width == 1) {
            const unsigned shift = 8 * (address & 1);
            write_io(address & 0x3fe, std::uint16_t(value << shift), std::uint16_t(0xff << shift));
        } else {
            write_io(address & 0x3fe, std::uint16_t(value), 0xffff);
            if (width == 4) write_io((address & 0x3fe) + 2, std::uint16_t(value >> 16), 0xffff);
        }
        break;
    case 5:
        if (width == 1) write_le(palette_, address & 0x3fe, 2, value * 0x101);
        else write_le(palette_, address & 0x3ff, width, value);
        break;
    case 6: {
        const auto offset = vram_offset(address);
        const unsigned obj_start = (io_[0] & 7) >= 3 ? 0x14000 : 0x10000;
        if (width == 1) {
            if (offset < obj_start) write_le(vram_, offset & ~1u, 2, value * 0x101);
        } else {
            write_le(vram_, offset, width, value);
        }
        break;
    }
    case 7:
        if (width != 1) write_le(oam_, address & 0x3ff, width, value);
        break;
    }
}

std::uint32_t Bus::read(std::uint32_t address, unsigned width, Access kind) {
    if (cpu_cursor_ < cpu_count_) return cpu_results_[cpu_cursor_++];
    const bool cart_byte = width == 1 && address >= entry && address < 0x0e000000;
    const auto beat = cart_byte ? address & ~1u : address;
    const auto duration = access_ticks(address, width, previous_.follows(beat, kind));
    advance_cpu_access(duration);
    previous_ = {beat, cart_byte ? 2u : width, kind};
    const auto value = read_data(address, width, instruction_address_ < bios_size);
    if (cpu_bounded_) {
        assert(cpu_count_ < cpu_results_.size());
        cpu_results_[cpu_count_++] = value;
        ++cpu_cursor_;
    }
    return value;
}

std::uint32_t Bus::fetch(std::uint32_t address, unsigned width) {
    instruction_address_ = address;
    if (address == entry && !entry_fetch_)
        entry_fetch_ = EntryFetch{ticks_, read_data(address, width, false), width};
    const auto opcode = read(address, width, Access::fetch);
    opcode_latch_ = width == 4 ? opcode : opcode * 0x10001;
    if (address < bios_size) {
        // The protection latch is driven by the BIOS word at the pipeline head.
        const auto ahead = std::min((address + 2 * width) & ~3u, std::uint32_t(bios_size - 4));
        bios_latch_ = read_le(bios_, ahead, 4);
    }
    return opcode;
}

void Bus::write(std::uint32_t address, unsigned width, std::uint32_t value) {
    if (cpu_cursor_ < cpu_count_) {
        ++cpu_cursor_;
        return;
    }
    advance_cpu_access(access_ticks(address, width, previous_.follows(address, Access::store)));
    previous_ = {address, width, Access::store};
    write_data(address, width, value);
    if (cpu_bounded_) {
        assert(cpu_count_ < cpu_results_.size());
        ++cpu_count_;
        ++cpu_cursor_;
    }
}

std::uint32_t Bus::fetch_arm(std::uint32_t address) { return fetch(address, 4); }
std::uint16_t Bus::fetch_thumb(std::uint32_t address) { return std::uint16_t(fetch(address, 2)); }
std::uint32_t Bus::load_word(std::uint32_t address) { return read(address, 4, Access::load); }
std::uint16_t Bus::load_half(std::uint32_t address) { return std::uint16_t(read(address, 2, Access::load)); }
std::uint8_t Bus::load_byte(std::uint32_t address) { return std::uint8_t(read(address, 1, Access::load)); }
void Bus::store_word(std::uint32_t address, std::uint32_t value) { write(address, 4, value); }
void Bus::store_half(std::uint32_t address, std::uint16_t value) { write(address, 2, value); }
void Bus::store_byte(std::uint32_t address, std::uint8_t value) { write(address, 1, value); }

void Bus::write_dma(unsigned address, std::uint16_t value, std::uint16_t mask) {
    const unsigned reg = address - dma_start, channel = reg / 12;
    auto& dma = dma_[channel];
    const auto merge = [=](std::uint32_t old, unsigned shift) {
        return (old & ~(std::uint32_t(mask) << shift)) | (std::uint32_t(value & mask) << shift);
    };
    const unsigned count_mask = channel == 3 ? 0xffff : 0x3fff;
    switch (reg % 12) {
    case 0: case 2:
        dma.source = merge(dma.source, 8 * (reg % 12)) & (channel == 0 ? 0x07ffffff : 0x0fffffff);
        break;
    case 4: case 6:
        dma.destination = merge(dma.destination, 8 * (reg % 12 - 4)) & (channel == 3 ? 0x0fffffff : 0x07ffffff);
        break;
    case 8: dma.count = merge(dma.count, 0) & count_mask; break;
    case 10: {
        const bool enabled = dma.control & 0x8000;
        dma.control = merge(dma.control, 0) & (channel == 3 ? 0xffe0 : 0xf7e0);
        if (!(dma.control & 0x8000)) {
            dma.pending = false;
            dma.remaining = 0;
        } else if (!enabled) {
            const unsigned align = (dma.control & 0x0400) ? ~3u : ~1u;
            dma.current_source = dma.source & align;
            dma.current_destination = dma.destination & align;
            dma.remaining = dma.count ? dma.count : count_mask + 1;
            dma.pending = (dma.control & 0x3800) == 0;
        }
        break;
    }
    }
}

void Bus::trigger_dma(unsigned timing) {
    for (auto& dma : dma_) {
        if ((dma.control & 0xb000) == (0x8000 | (timing << 12))) dma.pending = true;
    }
}

bool Bus::dma_pending() const noexcept {
    return dma_transfer_ || std::ranges::any_of(dma_, [](const Dma& channel) { return channel.pending; });
}

void Bus::step_dma(std::uint64_t limit) {
    if (!dma_transfer_) {
        const auto selected = std::ranges::find_if(dma_, [](const Dma& channel) { return channel.pending; });
        if (selected == dma_.end()) return;
        const unsigned channel = unsigned(selected - dma_.begin());
        const bool switching = dma_owner_ != channel;
        dma_transfer_ = DmaTransfer{channel, switching ? 2u : 0u, 0, DmaPhase::startup};
        if (switching) {
            branch();
            selected->reads = selected->writes = {};
            dma_owner_ = channel;
            emit(TimingKind::dma_start, channel);
        }
    }
    auto& transfer = *dma_transfer_;
    auto& dma = dma_[transfer.channel];
    const unsigned width = (dma.control & 0x0400) ? 4 : 2;
    const auto source = dma.current_source, destination = dma.current_destination;
    if (transfer.phase == DmaPhase::startup) {
        if (!consume(transfer.clocks, limit)) return;
        transfer.phase = DmaPhase::read;
        transfer.clocks = access_ticks(source, width, dma.reads.follows(source, Access::load));
    }
    if (transfer.phase == DmaPhase::read) {
        if (!consume(transfer.clocks, limit)) return;
        transfer.value = read_data(source, width, false);
        transfer.phase = DmaPhase::write;
        transfer.clocks = access_ticks(destination, width, dma.writes.follows(destination, Access::store));
    }
    if (!consume(transfer.clocks, limit)) return;
    const auto finished = transfer;
    dma_transfer_.reset();
    finish_dma_unit(dma, finished.channel, width, finished.value);
}

void Bus::finish_dma_unit(Dma& dma, unsigned channel, unsigned width, std::uint32_t value) {
    const auto source = dma.current_source, destination = dma.current_destination;
    const unsigned dest_mode = (dma.control >> 5) & 3;
    const unsigned source_mode = (dma.control >> 7) & 3;
    dma.reads = {source, width, Access::load};
    dma.writes = {destination, width, Access::store};
    dma.current_source = next_dma_address(source, source_mode, width);
    dma.current_destination = next_dma_address(destination, dest_mode, width);
    const bool completed = --dma.remaining == 0;
    if (completed) {
        dma.pending = false;
        dma_owner_.reset();
        if (dma.control & 0x4000) if_ |= std::uint16_t(0x100u << channel);
        if ((dma.control & 0x3000) && (dma.control & 0x0200)) {
            dma.remaining = dma.count ? dma.count : (channel == 3 ? 0x10000 : 0x4000);
            if (dest_mode == 3) dma.current_destination = dma.destination & ~(width - 1);
        } else {
            dma.control &= ~0x8000;
        }
    }
    write_data(destination, width, value);
    if (completed) emit(TimingKind::dma_complete, channel);
    if (wake()) halted_ = false;
}

}
