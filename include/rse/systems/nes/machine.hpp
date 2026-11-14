// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <rse/core/component.hpp>
#include <rse/devices/nes_controller.hpp>
#include <rse/devices/rp2a03_oam_dma.hpp>
#include <rse/systems/nes/cartridge.hpp>
#include <rse/systems/nes/cpu.hpp>
#include <rse/video/rp2c02.hpp>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>

namespace rse::nes {
struct Profile {
    // Integer master-clock offsets: PPU 0..3, CPU 0..11. Phase 0 first
    // clocks after a full period, not at reset tick zero.
    std::uint8_t ppu_phase{}, cpu_phase{};
    bool dma_get_on_odd{true};
    bool trace{};
    std::size_t trace_limit{1'000'000};
};
enum class TraceKind : std::uint8_t {
    cpu_read, cpu_write, dma_halt, dma_align, dma_get, dma_put,
    ppu_read, ppu_write, nmi, irq, input, frame
};
struct TraceEvent {
    Tick tick{};
    TraceKind kind{};
    std::uint16_t address{};
    std::uint64_t data{}, cycle{};
    bool operator==(const TraceEvent&) const = default;
};
struct MachineState {
    Tick tick{};
    CpuState cpu;
    video::PpuState ppu;
    std::uint64_t cpu_cycles{};
    std::uint8_t bus_latch{}, buttons{};
    chips::Cd4021State controller;
    devices::OamDmaState dma;
    bool instruction_boundary{}, irq{}, nmi{}, faulted{};
};

// Headless NTSC NROM machine. See docs/NES_SYSTEM.md for scheduling, reset,
// open-bus policy, input timestamps, trace bounds, and verification boundaries.
class Machine {
public:
    explicit Machine(Cartridge cartridge, Profile profile = {});
    ~Machine();
    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;
    void advance_to(Tick target);
    // Stop at the next PPU publication; frame length is not a fixed tick count.
    [[nodiscard]] const video::PpuFrame& run_frame();
    // Deterministic new power-on timeline for the same cartridge/profile.
    void reset();
    // Events must be strictly in the future. Equal-time insertion order is stable.
    void schedule_input(Tick tick, std::uint8_t buttons);
    void schedule_irq(Tick tick, bool asserted);
    [[nodiscard]] MachineState state() const noexcept;
    [[nodiscard]] const video::PpuFrame& frame() const noexcept;
    // RAM/PRG only; device ports return nullopt, with no read side effects.
    [[nodiscard]] std::optional<std::uint8_t> inspect_memory(std::uint16_t address) const noexcept;
    // Diagnostic views, invalidated by advance/reset. No frozen trace-file ABI.
    [[nodiscard]] std::span<const TraceEvent> trace() const noexcept;
private:
    struct Impl;
    Cartridge initial_;
    Profile profile_;
    std::unique_ptr<Impl> impl_;
};
} // namespace rse::nes
