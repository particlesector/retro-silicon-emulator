// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <rse/video/rp2c02.hpp>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

struct PpuAccess {
    std::uint16_t address;
    std::uint8_t value;
    bool writing;
    std::uint64_t dot;
    bool operator==(const PpuAccess&) const = default;
};
struct PpuTestBus final : rse::video::PpuBus {
    std::array<std::uint8_t, 16384> ram{};
    std::vector<PpuAccess> trace;
    bool recording{true};
    bool fail{};
    std::uint8_t read(std::uint16_t address, std::uint64_t dot) override {
        if (fail) throw std::runtime_error("PPU memory failure");
        if (recording) trace.push_back({address, ram.at(address), false, dot});
        return ram.at(address);
    }
    void write(std::uint16_t address, std::uint8_t value, std::uint64_t dot) override {
        if (fail) throw std::runtime_error("PPU memory failure");
        if (recording) trace.push_back({address, value, true, dot});
        ram.at(address) = value;
    }
};
inline void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
inline void clocks(rse::video::Rp2c02& ppu, std::uint64_t count) {
    for (std::uint64_t i = 0; i < count; ++i) ppu.clock();
}
inline void until(rse::video::Rp2c02& ppu, unsigned line, unsigned dot) {
    for (unsigned i = 0; i < 2 * 341 * 262; ++i) {
        const auto state = ppu.state();
        if (state.scanline == line && state.dot == dot) return;
        ppu.clock();
    }
    throw std::runtime_error("PPU did not reach target position");
}
inline void address(rse::video::Rp2c02& ppu, unsigned value) {
    (void)ppu.cpu_read(2);
    ppu.cpu_write(6, static_cast<std::uint8_t>(value >> 8));
    ppu.cpu_write(6, static_cast<std::uint8_t>(value));
}
inline void memory(rse::video::Rp2c02& ppu, unsigned where, std::uint8_t value) {
    address(ppu, where); ppu.cpu_write(7, value);
}
inline void oam(rse::video::Rp2c02& ppu, const std::array<std::uint8_t, 256>& data) {
    ppu.cpu_write(3, 0);
    for (auto byte : data) ppu.cpu_write(4, byte);
}
inline std::uint32_t frame_hash(const rse::video::PpuFrame& frame) {
    std::uint32_t hash = 2166136261u;
    for (auto pixel : frame.samples) {
        hash = (hash ^ (pixel & 255)) * 16777619u;
        hash = (hash ^ (pixel >> 8)) * 16777619u;
    }
    return hash;
}
