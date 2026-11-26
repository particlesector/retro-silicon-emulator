// SPDX-License-Identifier: Apache-2.0
#include <rse/systems/gba/bus.hpp>
#include <algorithm>
#include <stdexcept>

namespace rse::gba {
namespace {
constexpr unsigned line_ticks = 1232, hblank_tick = 1006, frame_lines = 228;
constexpr unsigned dma_start = 0xb0, dma_end = 0xe0;
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

void Bus::advance(unsigned ticks) {
    while (ticks >= ticks_to_lcd_event()) {
        const auto span = ticks_to_lcd_event();
        ticks -= span;
        ticks_ += span;
        dot_ += span;
        const auto stat = io_[2];
        if (dot_ == hblank_tick) {
            if (stat & 0x10) if_ |= 2;
            if (line_ < 160) trigger_dma(2);
        } else {
            dot_ = 0;
            line_ = (line_ + 1) % frame_lines;
            if (line_ == 160) {
                if (stat & 8) if_ |= 1;
                trigger_dma(1);
            }
            if (line_ == (stat >> 8) && (stat & 0x20)) if_ |= 4;
        }
    }
    ticks_ += ticks;
    dot_ += ticks;
    if (wake()) halted_ = false;
}

void Bus::idle_to_lcd_event() { advance(ticks_to_lcd_event()); }

std::uint16_t Bus::read_io(unsigned address) const {
    switch (address) {
    case 0x004:
        return io_[2] | (line_ >= 160 && line_ < 227 ? 1 : 0) |
            (dot_ >= hblank_tick ? 2 : 0) | (line_ == (io_[2] >> 8) ? 4 : 0);
    case 0x006: return std::uint16_t(line_);
    case 0x130: return 0x03ff; // All keys released.
    case 0x200: return ie_;
    case 0x202: return if_;
    case 0x204: return waitcnt_;
    case 0x208: return ime_;
    default:
        if (address >= dma_start && address < dma_end) {
            const unsigned reg = address - dma_start;
            return reg % 12 == 10 ? dma_[reg / 12].control : 0;
        }
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
    case 0x200: ie_ = merge(ie_) & 0x3fff; break;
    case 0x202: if_ &= ~(value & mask); break;
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
        else io_[address / 2] = merge(io_[address / 2]);
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
    const bool cart_byte = width == 1 && address >= entry && address < 0x0e000000;
    const auto beat = cart_byte ? address & ~1u : address;
    const auto duration = access_ticks(address, width, previous_.follows(beat, kind));
    previous_ = {beat, cart_byte ? 2u : width, kind};
    advance(duration);
    return read_data(address, width, instruction_address_ < bios_size);
}

std::uint32_t Bus::fetch(std::uint32_t address, unsigned width) {
    const auto tick = ticks_;
    instruction_address_ = address;
    const auto opcode = read(address, width, Access::fetch);
    opcode_latch_ = width == 4 ? opcode : opcode * 0x10001;
    if (address < bios_size) {
        // The protection latch is driven by the BIOS word at the pipeline head.
        const auto ahead = std::min((address + 2 * width) & ~3u, std::uint32_t(bios_size - 4));
        bios_latch_ = read_le(bios_, ahead, 4);
    }
    if (address == entry && !entry_fetch_) entry_fetch_ = EntryFetch{tick, opcode, width};
    return opcode;
}

void Bus::write(std::uint32_t address, unsigned width, std::uint32_t value) {
    advance(access_ticks(address, width, previous_.follows(address, Access::store)));
    previous_ = {address, width, Access::store};
    write_data(address, width, value);
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
    return std::ranges::any_of(dma_, [](const Dma& channel) { return channel.pending; });
}

void Bus::step_dma() {
    const auto selected = std::ranges::find_if(dma_, [](const Dma& channel) { return channel.pending; });
    if (selected == dma_.end()) return;
    auto& dma = *selected;
    const unsigned channel = unsigned(selected - dma_.begin());
    const unsigned width = (dma.control & 0x0400) ? 4 : 2;
    const auto source = dma.current_source, destination = dma.current_destination;
    const unsigned dest_mode = (dma.control >> 5) & 3;
    const unsigned source_mode = (dma.control >> 7) & 3;
    const bool new_owner = dma_owner_ != channel;
    if (new_owner) {
        branch();
        dma.reads = dma.writes = {};
        dma_owner_ = channel;
        advance(2);
    }
    advance(access_ticks(source, width, dma.reads.follows(source, Access::load)));
    const auto value = read_data(source, width, false);
    advance(access_ticks(destination, width, dma.writes.follows(destination, Access::store)));
    dma.reads = {source, width, Access::load};
    dma.writes = {destination, width, Access::store};
    dma.current_source = next_dma_address(source, source_mode, width);
    dma.current_destination = next_dma_address(destination, dest_mode, width);
    if (--dma.remaining == 0) {
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
}

}
