// SPDX-License-Identifier: Apache-2.0
#include <rse/video/rp2c02.hpp>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace rse::video {
struct Rp2c02::Impl {
    struct Sprite {
        std::uint8_t x{}, low{}, high{}, attributes{}, identity{0xff};
        bool valid{};
    };
    PpuBus& bus;
    PpuState r;
    std::uint8_t io{}, buffer{}, oam_bus{0xff};
    std::array<std::uint8_t, 256> oam;
    std::array<std::uint8_t, 32> secondary, palette{};
    std::array<std::uint8_t, 8> identities;
    std::array<Sprite, 8> sprites{};
    std::array<PpuFrame, 2> frames{};
    unsigned drawing{};
    std::uint16_t bg_low{}, bg_high{}, attr_low{}, attr_high{};
    std::uint8_t next_tile{}, next_attribute{}, next_low{}, next_high{};
    std::uint16_t fetch_address{}, sprite_address{};
    unsigned eval_n{}, eval_m{}, selected{}, secondary_position{}, overflow_remaining{};
    std::uint8_t eval_latch{};
    bool eval_done{}, suppress_vblank{};
    std::array<std::uint8_t, 3> render_delay{};
    std::uint8_t render_enable{};

    explicit Impl(PpuBus& connection) : bus(connection) {
        r.scanline = 261;
        oam.fill(0xff); secondary.fill(0xff); identities.fill(0xff);
        for (unsigned i = 2; i < oam.size(); i += 4) oam[i] = 0xe3;
    }
    bool enabled() const noexcept { return render_enable != 0; }
    bool render_line() const noexcept { return r.scanline < 240 || r.scanline == 261; }
    bool rendering() const noexcept { return enabled() && render_line(); }
    bool nmi() const noexcept { return (r.control & 0x80) && (r.status & 0x80); }
    void healthy() const {
        if (r.faulted) throw std::logic_error("PPU requires reset after failure");
    }
    static unsigned palette_index(std::uint16_t address) noexcept {
        unsigned index = address & 31;
        if ((index & 0x13) == 0x10) index &= 15;
        return index;
    }
    std::uint8_t palette_read(std::uint16_t address) const noexcept {
        return palette[palette_index(address)] & ((r.mask & 1) ? 0x30 : 0x3f);
    }
    void increment_x() noexcept {
        if ((r.v & 31) == 31) r.v = (r.v & ~31) ^ 0x0400;
        else ++r.v;
    }
    void increment_y() noexcept {
        if ((r.v & 0x7000) != 0x7000) r.v += 0x1000;
        else {
            r.v &= ~0x7000;
            unsigned y = (r.v & 0x03e0) >> 5;
            if (y == 29) { y = 0; r.v ^= 0x0800; }
            else if (y == 31) y = 0;
            else ++y;
            r.v = (r.v & ~0x03e0) | (y << 5);
        }
    }
    void increment_data() noexcept {
        if (rendering()) { increment_x(); increment_y(); }
        else r.v = (r.v + ((r.control & 4) ? 32 : 1)) & 0x7fff;
    }
    std::uint8_t cpu_peek(std::uint16_t select) const noexcept {
        switch (select & 7) {
        case 2: return (r.status & 0xe0) | (io & 31);
        case 4:
            if (rendering()) return oam_bus;
            return oam[r.oam_address] & ((r.oam_address & 3) == 2 ? 0xe3 : 0xff);
        case 7:
            if ((r.v & 0x3fff) >= 0x3f00) return (io & 0xc0) | palette_read(r.v);
            return buffer;
        default: return io;
        }
    }
    std::uint8_t cpu_read(std::uint16_t select) {
        healthy();
        try {
            switch (select & 7) {
            case 2: {
                io = cpu_peek(select);
                if (r.scanline == 241 && r.dot <= 1) suppress_vblank = true;
                r.status &= ~0x80;
                r.write_toggle = false;
                break;
            }
            case 4:
                io = cpu_peek(select);
                break;
            case 7: {
                const auto address = r.v & 0x3fff;
                if (address >= 0x3f00) {
                    io = cpu_peek(select);
                    buffer = bus.read(address & 0x2fff, r.dots);
                } else {
                    io = buffer;
                    buffer = bus.read(address, r.dots);
                }
                increment_data();
                break;
            }
            default: break; // Write-only ports return the I/O latch.
            }
            return io;
        } catch (...) { r.faulted = true; throw; }
    }
    void cpu_write(std::uint16_t select, std::uint8_t value) {
        healthy();
        try {
            io = value;
            switch (select & 7) {
            case 0:
                r.control = value;
                r.t = (r.t & ~0x0c00) | ((value & 3) << 10);
                break;
            case 1: r.mask = value; break;
            case 3: r.oam_address = value; break;
            case 4:
                if (rendering()) r.oam_address += 4;
                else {
                    oam[r.oam_address] = (r.oam_address & 3) == 2 ? value & 0xe3 : value;
                    ++r.oam_address;
                }
                break;
            case 5:
                if (!r.write_toggle) {
                    r.t = (r.t & ~31) | (value >> 3);
                    r.fine_x = value & 7;
                } else r.t = (r.t & 0x0c1f) | ((value & 7) << 12) | ((value & 0xf8) << 2);
                r.write_toggle = !r.write_toggle;
                break;
            case 6:
                if (!r.write_toggle) r.t = (r.t & 0xff) | ((value & 0x3f) << 8);
                else { r.t = (r.t & 0x7f00) | value; r.v = r.t; }
                r.write_toggle = !r.write_toggle;
                break;
            case 7: {
                const auto address = r.v & 0x3fff;
                if (address >= 0x3f00) palette[palette_index(address)] = value & 0x3f;
                else bus.write(address, value, r.dots);
                increment_data();
                break;
            }
            default: break; // Status writes only refresh the I/O latch.
            }
        } catch (...) { r.faulted = true; throw; }
    }

    void background() {
        // Emit pixels before shifting. Prefetches leave tile 0 in the high byte
        // and tile 1 in the low byte at the start of the visible scanline.
        bg_low <<= 1; bg_high <<= 1; attr_low <<= 1; attr_high <<= 1;
        switch (r.dot & 7) {
        case 1: fetch_address = 0x2000 | (r.v & 0x0fff); break;
        case 2: next_tile = bus.read(fetch_address, r.dots + 1); break;
        case 3:
            fetch_address = 0x23c0 | (r.v & 0x0c00) | ((r.v >> 4) & 0x38) | ((r.v >> 2) & 7);
            break;
        case 4: {
            const auto attribute = bus.read(fetch_address, r.dots + 1);
            next_attribute = (attribute >> (((r.v >> 4) & 4) | (r.v & 2))) & 3;
            break;
        }
        case 5:
            fetch_address = ((r.control & 0x10) ? 0x1000 : 0) | (next_tile << 4) | ((r.v >> 12) & 7);
            break;
        case 6: next_low = bus.read(fetch_address, r.dots + 1); break;
        case 7: fetch_address += 8; break;
        case 0:
            next_high = bus.read(fetch_address, r.dots + 1);
            bg_low = (bg_low & 0xff00) | next_low;
            bg_high = (bg_high & 0xff00) | next_high;
            attr_low = (attr_low & 0xff00) | ((next_attribute & 1) ? 0xff : 0);
            attr_high = (attr_high & 0xff00) | ((next_attribute & 2) ? 0xff : 0);
            increment_x();
            break;
        }
    }
    bool in_range(std::uint8_t y) const noexcept {
        // No 8-bit wrap: Y >= 239 hides sprites rather than wrapping to the top.
        const int row = int(r.scanline) - y;
        return row >= 0 && row < ((r.control & 0x20) ? 16 : 8);
    }
    void evaluate_sprites() {
        if (r.dot <= 64) {
            oam_bus = 0xff;
            if (!(r.dot & 1)) secondary[r.dot / 2 - 1] = 0xff;
            return;
        }
        if (r.dot == 65) {
            if (r.oam_address) {
                // G/H OAM refresh quirk: copy the addressed 8-byte row to row 0.
                std::array<std::uint8_t, 8> row{};
                std::copy_n(oam.begin() + (r.oam_address & 0xf8), 8, row.begin());
                std::copy(row.begin(), row.end(), oam.begin());
            }
            eval_n = r.oam_address >> 2; eval_m = r.oam_address & 3;
            selected = secondary_position = overflow_remaining = 0;
            eval_done = false;
            identities.fill(0xff);
        }
        if (r.dot & 1) {
            eval_latch = oam[r.oam_address];
            oam_bus = eval_latch;
            return;
        }
        if (selected >= 8) oam_bus = secondary[0];
        if (eval_done) { eval_n = (eval_n + 1) & 63; eval_m = 0; }
        else if (selected < 8) {
            secondary[secondary_position] = eval_latch;
            if (eval_m == 0) {
                if (in_range(eval_latch)) {
                    identities[selected] = static_cast<std::uint8_t>(eval_n);
                    ++secondary_position; ++eval_m;
                } else ++eval_n;
            } else {
                ++secondary_position;
                eval_m = (eval_m + 1) & 3;
                if (eval_m == 0) { ++selected; ++eval_n; }
            }
        } else if (overflow_remaining) {
            eval_m = (eval_m + 1) & 3;
            if (!eval_m) ++eval_n;
            if (!--overflow_remaining) eval_done = true;
        } else if (in_range(eval_latch)) {
            r.status |= 0x20;
            eval_m = (eval_m + 1) & 3;
            if (!eval_m) ++eval_n;
            overflow_remaining = 3;
        } else {
            ++eval_n;
            eval_m = (eval_m + 1) & 3; // Hardware's diagonal overflow scan.
        }
        if (eval_n >= 64) { eval_n = 0; eval_done = true; }
        r.oam_address = static_cast<std::uint8_t>((eval_n << 2) | eval_m);
    }
    static std::uint8_t reverse(std::uint8_t v) noexcept {
        v = ((v & 0x55) << 1) | ((v >> 1) & 0x55);
        v = ((v & 0x33) << 2) | ((v >> 2) & 0x33);
        return static_cast<std::uint8_t>((v << 4) | (v >> 4));
    }
    void fetch_sprites() {
        const unsigned slot = (r.dot - 257) / 8;
        const unsigned phase = (r.dot - 257) & 7;
        const auto* data = secondary.data() + slot * 4;
        auto& sprite = sprites[slot];
        r.oam_address = 0;
        oam_bus = data[std::min(phase, 3u)];
        if (phase == 0) {
            sprite = {};
            sprite.x = data[3]; sprite.attributes = data[2]; sprite.identity = identities[slot];
            sprite.valid = r.scanline < 240 && slot < selected;
        }
        if (phase == 1 || phase == 3) (void)bus.read(0x2000 | (r.v & 0x0fff), r.dots + 1);
        if (phase == 4) {
            const unsigned height = (r.control & 0x20) ? 16 : 8;
            unsigned row = (unsigned(r.scanline) - data[0]) & (height - 1);
            if (data[2] & 0x80) row = height - 1 - row;
            if (height == 16)
                sprite_address = ((data[1] & 1) << 12) | ((data[1] & 0xfe) << 4) | ((row & 8) << 1) | (row & 7);
            else sprite_address = ((r.control & 8) ? 0x1000 : 0) | (data[1] << 4) | row;
        }
        if (phase == 5) sprite.low = bus.read(sprite_address, r.dots + 1);
        if (phase == 7) {
            sprite.high = bus.read(sprite_address + 8, r.dots + 1);
            if (sprite.attributes & 0x40) {
                sprite.low = reverse(sprite.low); sprite.high = reverse(sprite.high);
            }
        }
    }
    void pixel() {
        const unsigned x = r.dot - 1;
        std::uint8_t bg{}, bg_palette{}, sp{}, sp_palette{}, sp_attributes{}, sp_identity{0xff};
        if ((render_enable & 8) && (x >= 8 || (r.mask & 2))) {
            const auto bit = 0x8000u >> r.fine_x;
            bg = ((bg_low & bit) ? 1 : 0) | ((bg_high & bit) ? 2 : 0);
            bg_palette = ((attr_low & bit) ? 1 : 0) | ((attr_high & bit) ? 2 : 0);
        }
        if (enabled()) for (auto& sprite : sprites) {
            if (sprite.x) { --sprite.x; continue; }
            const std::uint8_t value = ((sprite.low & 0x80) ? 1 : 0) | ((sprite.high & 0x80) ? 2 : 0);
            if (!sp && sprite.valid && value && (render_enable & 0x10) && (x >= 8 || (r.mask & 4))) {
                sp = value; sp_palette = sprite.attributes & 3;
                sp_attributes = sprite.attributes; sp_identity = sprite.identity;
            }
            sprite.low <<= 1; sprite.high <<= 1;
        }
        if (bg && sp && sp_identity == 0 && x != 255) r.status |= 0x40;
        std::uint16_t index = bg ? (bg_palette << 2) | bg : 0;
        if (sp && (!bg || !(sp_attributes & 0x20))) index = 0x10 | (sp_palette << 2) | sp;
        if (!enabled() && (r.v & 0x3f00) == 0x3f00) index = r.v;
        frames[drawing].samples[r.scanline * 256 + x] = palette_read(index) | ((r.mask & 0xe0) << 1);
    }
    void clock() {
        healthy();
        try {
            if (r.dots == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("PPU dot counter exhausted");
            if (r.scanline == 261 && r.dot == 1) {
                r.status &= ~0xe0;
                suppress_vblank = false;
            }
            if (r.scanline == 241 && r.dot == 1) {
                if (!suppress_vblank) r.status |= 0x80;
                frames[drawing].number = ++r.frames;
                drawing ^= 1;
            }
            if (r.scanline < 240 && r.dot >= 1 && r.dot <= 256) pixel();
            if (rendering()) {
                if ((r.dot >= 1 && r.dot <= 256) || (r.dot >= 321 && r.dot <= 336)) background();
                if (r.dot == 256) increment_y();
                if (r.dot == 257) r.v = (r.v & ~0x041f) | (r.t & 0x041f);
                if (r.scanline == 261 && r.dot >= 280 && r.dot <= 304)
                    r.v = (r.v & ~0x7be0) | (r.t & 0x7be0);
                if (r.scanline < 240 && r.dot >= 1 && r.dot <= 256) evaluate_sprites();
                if (r.scanline == 261 && r.dot == 257) {
                    secondary.fill(0xff); identities.fill(0xff); selected = 0;
                }
                if (r.dot >= 257 && r.dot <= 320) fetch_sprites();
                if (r.dot == 338 || r.dot == 340) (void)bus.read(0x2000 | (r.v & 0x0fff), r.dots + 1);
            }
            ++r.dots;
            if (r.scanline == 261 && r.dot == 339 && r.odd && enabled()) {
                r.scanline = 0; r.dot = 0;
            } else if (++r.dot == 341) {
                r.dot = 0;
                if (r.scanline == 260) r.odd = !r.odd;
                r.scanline = (r.scanline + 1) % 262;
            }
            // Whole-dot model of the rendering-enable pipeline. A write at
            // boundary T affects pixel/fetch logic starting at boundary T+3.
            render_delay[2] = render_delay[1]; render_delay[1] = render_delay[0];
            render_delay[0] = r.mask & 0x18; render_enable = render_delay[2];
        } catch (...) { r.faulted = true; throw; }
    }
};

Rp2c02::Rp2c02(PpuBus& bus) : impl_(std::make_unique<Impl>(bus)) {}
Rp2c02::~Rp2c02() = default;
void Rp2c02::clock() { impl_->clock(); }
void Rp2c02::reset() { impl_ = std::make_unique<Impl>(impl_->bus); }
std::uint8_t Rp2c02::cpu_read(std::uint16_t select) { return impl_->cpu_read(select); }
std::uint8_t Rp2c02::cpu_peek(std::uint16_t select) const noexcept { return impl_->cpu_peek(select); }
std::uint8_t Rp2c02::inspect_palette(std::uint16_t address) const noexcept {
    return impl_->palette[Impl::palette_index(address)];
}
std::uint8_t Rp2c02::inspect_oam(std::uint8_t address) const noexcept { return impl_->oam[address]; }
void Rp2c02::cpu_write(std::uint16_t select, std::uint8_t value) { impl_->cpu_write(select, value); }
bool Rp2c02::nmi_asserted() const noexcept { return impl_->nmi(); }
PpuState Rp2c02::state() const noexcept {
    auto state = impl_->r;
    state.nmi = impl_->nmi();
    return state;
}
const PpuFrame& Rp2c02::frame() const noexcept { return impl_->frames[impl_->drawing ^ 1]; }
} // namespace rse::video
