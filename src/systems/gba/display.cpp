// SPDX-License-Identifier: Apache-2.0
#include <rse/systems/gba/display.hpp>
#include <algorithm>

namespace rse::gba {
namespace {
constexpr unsigned backdrop = 5, object = 4;
constexpr std::uint16_t transparent = 0x8000;

std::uint16_t half(std::span<const std::uint8_t> bytes, unsigned offset) {
    return bytes[offset] | (unsigned(bytes[offset + 1]) << 8);
}

int coefficient(std::uint16_t value) { return static_cast<std::int16_t>(value); }

unsigned tile_pixel(Display::Vram vram, unsigned address, unsigned x, unsigned y, bool full_palette) {
    if (full_palette) return vram[address + y * 8 + x];
    return (vram[address + y * 4 + x / 2] >> ((x & 1) * 4)) & 15;
}

struct Pixel {
    std::uint16_t color{transparent};
    unsigned priority{4};
    unsigned layer{backdrop};
    bool semi{};
    unsigned order() const { return priority * 8 + ((layer == object) ? 0 : layer + 1); }
};

struct Objects {
    std::array<Pixel, screen_width> pixels{};
    std::array<bool, screen_width> window{};
};

struct Scanline {
    Display::Registers regs;
    Display::Vram vram;
    Display::Palette palette;
    Display::Oam oam;
    unsigned y;

    std::uint16_t reg(unsigned address) const { return regs[address / 2]; }
    std::uint16_t color(unsigned index) const { return half(palette, index * 2) & 0x7fff; }
    unsigned mode() const { return reg(0) & 7; }
    std::uint16_t text(unsigned bg, unsigned x, unsigned source_y) const;
    std::uint16_t affine(unsigned bg, int x, int source_y) const;
    std::uint16_t bitmap(int x, int source_y) const;
    Objects objects() const;
    unsigned window_mask(unsigned x, bool obj_window) const;
    std::uint16_t compose(const Pixel& top, const Pixel& next, bool effects) const;
};

std::uint16_t Scanline::text(unsigned bg, unsigned x, unsigned source_y) const {
    const auto control = reg(8 + bg * 2);
    const auto size = control >> 14;
    x = (x + reg(0x10 + bg * 4)) & (size & 1 ? 511 : 255);
    source_y = (source_y + reg(0x12 + bg * 4)) & (size & 2 ? 511 : 255);
    const auto block = x / 256 + (source_y / 256) * ((size & 1) ? 2 : 1);
    const auto map = (((control >> 8) & 31) + block) * 0x800;
    const auto entry = half(vram, (map + 2 * ((source_y / 8 & 31) * 32 + (x / 8 & 31))) & 0xffff);
    x = (x & 7) ^ (entry & 0x400 ? 7u : 0u);
    source_y = (source_y & 7) ^ (entry & 0x800 ? 7u : 0u);
    const bool full_palette = control & 0x80;
    const auto tile = ((control >> 2) & 3) * 0x4000 + (entry & 0x3ff) * (full_palette ? 64 : 32);
    const auto index = tile_pixel(vram, tile & 0xffff, x, source_y, full_palette);
    if (index == 0) return transparent;
    return color(index + (full_palette ? 0 : (entry >> 12) * 16));
}

std::uint16_t Scanline::affine(unsigned bg, int x, int source_y) const {
    const auto control = reg(8 + bg * 2);
    const int size = 128 << (control >> 14);
    if (control & 0x2000) {
        x &= size - 1;
        source_y &= size - 1;
    } else if (x < 0 || source_y < 0 || x >= size || source_y >= size) {
        return transparent;
    }
    const auto map = ((control >> 8) & 31) * 0x800;
    const auto tile = vram[(map + source_y / 8 * (size / 8) + x / 8) & 0xffff];
    const auto base = ((control >> 2) & 3) * 0x4000 + tile * 64;
    const auto index = tile_pixel(vram, base, x & 7, source_y & 7, true);
    return index ? color(index) : transparent;
}

std::uint16_t Scanline::bitmap(int x, int source_y) const {
    const auto kind = mode();
    const int width = kind == 5 ? 160 : 240, height = kind == 5 ? 128 : 160;
    if (x < 0 || source_y < 0 || x >= width || source_y >= height) return transparent;
    const unsigned pixel = source_y * width + x;
    const unsigned page = kind != 3 && (reg(0) & 0x10) ? 0xa000 : 0;
    if (kind == 4) {
        const auto index = vram[page + pixel];
        return index ? color(index) : transparent;
    }
    return half(vram, page + pixel * 2) & 0x7fff;
}

Objects Scanline::objects() const {
    Objects result;
    constexpr int sizes[3][4][2] = {
        {{8,8}, {16,16}, {32,32}, {64,64}},
        {{16,8}, {32,8}, {32,16}, {64,32}},
        {{8,16}, {8,32}, {16,32}, {32,64}},
    };
    const bool linear = reg(0) & 0x40;
    for (unsigned offset = 0; offset < oam.size(); offset += 8) {
        const auto a = half(oam, offset), b = half(oam, offset + 2), c = half(oam, offset + 4);
        const bool transformed = a & 0x100, double_size = a & 0x200;
        const unsigned shape = a >> 14, style = (a >> 10) & 3;
        if ((!transformed && double_size) || shape == 3 || style == 3 ||
            (style == 2 && !(reg(0) & 0x8000))) continue;
        const auto& size = sizes[shape][b >> 14];
        const int width = size[0], height = size[1];
        const int box_width = double_size ? width * 2 : width, box_height = double_size ? height * 2 : height;
        const int row = (y - (a & 255)) & 255;
        if (row >= box_height) continue;
        const int left = int((b & 0x1ff) ^ 0x100) - 0x100;
        const bool full_palette = a & 0x2000;
        const unsigned units = full_palette ? 2 : 1;
        const unsigned base_tile = (c & 0x3ff) & ~(full_palette && !linear ? 1u : 0u);
        if (mode() >= 3 && base_tile < 512) continue;
        const unsigned priority = (c >> 10) & 3;
        const unsigned pal = 256 + (full_palette ? 0 : (c >> 12) * 16);
        const unsigned matrix = ((b >> 9) & 31) * 32;
        const int pa = transformed ? coefficient(half(oam, matrix + 6)) : 0;
        const int pb = transformed ? coefficient(half(oam, matrix + 14)) : 0;
        const int pc = transformed ? coefficient(half(oam, matrix + 22)) : 0;
        const int pd = transformed ? coefficient(half(oam, matrix + 30)) : 0;
        const unsigned mosaic = a & 0x1000 ? reg(0x4c) >> 8 : 0;
        const int h_group = (mosaic & 15) + 1, v_group = (mosaic >> 4) + 1;
        const int sample_y = std::max(0, row - int(y % v_group));
        const int stride = linear ? width / 8 * units : 32;
        for (int x = std::max(0, left); x < std::min(int(screen_width), left + box_width); ++x) {
            auto& pixel = result.pixels[x];
            if (style != 2 && priority >= pixel.priority) continue;
            int tx = std::max(0, x - x % h_group - left), ty = sample_y;
            if (transformed) {
                const int dx = tx - box_width / 2, dy = ty - box_height / 2;
                tx = ((pa * dx + pb * dy) >> 8) + width / 2;
                ty = ((pc * dx + pd * dy) >> 8) + height / 2;
                if (tx < 0 || tx >= width || ty < 0 || ty >= height) continue;
            } else {
                if (b & 0x1000) tx = width - 1 - tx;
                if (b & 0x2000) ty = height - 1 - ty;
            }
            const unsigned tile_x = (tx / 8) * units;
            const unsigned column = linear ? base_tile + tile_x : (base_tile & ~31u) | ((base_tile + tile_x) & 31);
            const unsigned tile = (column + (ty / 8) * stride) & 0x3ff;
            const unsigned offset = tile * 32 + (ty & 7) * (full_palette ? 8 : 4) +
                                    (full_palette ? (tx & 7) : (tx & 7) / 2);
            const unsigned data = vram[0x10000 + (offset & 0x7fff)];
            const unsigned index = full_palette ? data : (data >> (4 * (tx & 1))) & 15;
            if (!index) continue;
            if (style == 2) result.window[x] = true;
            else pixel = {color(pal + index), priority, object, style == 1};
        }
    }
    return result;
}

bool in_window(unsigned position, unsigned edges) {
    const unsigned start = edges >> 8, end = edges & 255;
    return start <= end ? position >= start && position < end : position >= start || position < end;
}

unsigned Scanline::window_mask(unsigned x, bool obj_window) const {
    const auto enabled = reg(0) >> 13;
    if (!enabled) return 0x3f;
    for (unsigned i = 0; i < 2; ++i) {
        if ((enabled & (1u << i)) && in_window(x, reg(0x40 + i * 2)) && in_window(y, reg(0x44 + i * 2)))
            return (reg(0x48) >> (i * 8)) & 0x3f;
    }
    return (reg(0x4a) >> (obj_window ? 8 : 0)) & 0x3f;
}

std::uint16_t Scanline::compose(const Pixel& top, const Pixel& next, bool effects) const {
    if (!effects) return top.color;
    const auto control = reg(0x50);
    const unsigned kind = (control >> 6) & 3;
    const bool first = control & (1u << top.layer);
    const bool second = next.color != transparent && (control & (0x100u << next.layer));
    const bool alpha = (top.semi || (first && kind == 1)) && second;
    if (!alpha && (!first || kind < 2)) return top.color;
    const unsigned a = std::min(16u, reg(0x52) & 31u), b = std::min(16u, (reg(0x52) >> 8) & 31u);
    const unsigned brightness = std::min(16u, reg(0x54) & 31u);
    unsigned color = 0;
    for (unsigned shift = 0; shift < 15; shift += 5) {
        unsigned channel = (top.color >> shift) & 31;
        if (alpha) channel = std::min(31u, (channel * a + ((next.color >> shift) & 31) * b) / 16);
        else if (kind == 2) channel += (31 - channel) * brightness / 16;
        else channel -= channel * brightness / 16;
        color |= channel << shift;
    }
    return std::uint16_t(color);
}
}

void Display::reset() noexcept {
    number_ = 0;
    published_ = 0;
    references_.fill(0);
}

void Display::write_reference(unsigned address, Registers regs) noexcept {
    address &= ~3u;
    const auto raw = regs[address / 2] | (std::uint32_t(regs[address / 2 + 1] & 0xfff) << 16);
    const auto index = ((address - 0x28) / 16) * 2 + ((address & 4) ? 1 : 0);
    references_[index] = std::int32_t(raw ^ 0x08000000) - 0x08000000;
}

void Display::begin_frame(Registers regs) noexcept {
    for (auto address : {0x28u, 0x2cu, 0x38u, 0x3cu}) write_reference(address, regs);
}

void Display::draw(unsigned y, Registers regs, Vram vram, Palette palette, Oam oam) {
    const Scanline line{regs, vram, palette, oam, y};
    auto* output = frames_[1 - published_].data() + y * screen_width;
    const auto enabled = (regs[0] >> 8) & 31;
    if (regs[0] & 0x80) {
        std::fill_n(output, screen_width, 0x7fff);
    } else {
        constexpr unsigned text_layers[] = {15, 3, 0, 0, 0, 0, 0, 0};
        constexpr unsigned affine_layers[] = {0, 4, 12, 4, 4, 4, 0, 0};
        const unsigned mode = line.mode();
        const auto objects = enabled & 16 ? line.objects() : Objects{};
        for (unsigned x = 0; x < screen_width; ++x) {
            const auto window = line.window_mask(x, objects.window[x]);
            const auto visible = window & enabled;
            Pixel top{line.color(0), 4, backdrop}, next;
            auto insert = [&](Pixel pixel) {
                if (pixel.color == transparent) return;
                if (pixel.order() < top.order()) {
                    next = top;
                    top = pixel;
                } else if (next.color == transparent || pixel.order() < next.order()) {
                    next = pixel;
                }
            };
            if (visible & 16) insert(objects.pixels[x]);
            for (unsigned bg = 0; bg < 4; ++bg) {
                const auto bit = 1u << bg;
                if (!(visible & bit)) continue;
                const auto control = line.reg(8 + bg * 2);
                const auto mosaic = (control & 0x40) ? line.reg(0x4c) & 255 : 0;
                const int sample_x = x - x % ((mosaic & 15) + 1);
                const int repeat_y = y % ((mosaic >> 4) + 1);
                std::uint16_t color = transparent;
                if (text_layers[mode] & bit) {
                    color = line.text(bg, sample_x, y - repeat_y);
                } else if (affine_layers[mode] & bit) {
                    const unsigned base = 0x20 + 16 * (bg - 2);
                    const auto px = (references_[2 * (bg - 2)] + coefficient(line.reg(base)) * sample_x -
                                     coefficient(line.reg(base + 2)) * repeat_y) >> 8;
                    const auto py = (references_[2 * (bg - 2) + 1] + coefficient(line.reg(base + 4)) * sample_x -
                                     coefficient(line.reg(base + 6)) * repeat_y) >> 8;
                    color = mode < 3 ? line.affine(bg, px, py) : line.bitmap(px, py);
                }
                insert({color, control & 3u, bg});
            }
            output[x] = line.compose(top, next, window & 32);
        }
    }
    for (unsigned bg = 0; bg < 2; ++bg) {
        references_[bg * 2] += coefficient(line.reg(0x22 + 16 * bg));
        references_[bg * 2 + 1] += coefficient(line.reg(0x26 + 16 * bg));
    }
}

void Display::publish() noexcept {
    published_ = 1 - published_;
    ++number_;
}

}
