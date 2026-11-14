// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <rse/cpu/bus.hpp>
#include <memory>
#include <stdexcept>

namespace rse::cpu {
struct CpuState {
    std::uint16_t pc{};
    std::uint8_t s{}, a{}, x{}, y{}, p{0x24};
    bool operator==(const CpuState&) const = default;
};

enum class Variant { nmos6502, ricoh2a03 };
class UnsupportedOpcode : public std::runtime_error {
public:
    UnsupportedOpcode(std::uint8_t opcode, std::uint16_t address);
    const std::uint8_t opcode;
    const std::uint16_t address;
};

// Reusable NMOS 6502 instruction core. See docs/CPU_6502.md for cycle/interrupt
// conventions, initialization, unsupported operations, and verification limits.
class Mos6502 {
public:
    // Deterministic power-on state followed by a seven-cycle reset sequence.
    explicit Mos6502(Bus& bus, Variant variant = Variant::nmos6502);
    // Begin at an instruction boundary with explicit registers (test harnesses).
    // Not a mid-instruction save-state format. P bits 4/5 are canonicalized.
    Mos6502(Bus& bus, CpuState initial, Variant variant = Variant::nmos6502);
    ~Mos6502();
    Mos6502(const Mos6502&) = delete;
    Mos6502& operator=(const Mos6502&) = delete;
    // One CPU slot; granted accesses execute once, denied reads remain pending.
    // Interrupt sampling continues during holds. Exceptions fault until reset().
    void clock();
    // Abort current work and request reset on subsequent clocks. Preserves
    // A/X/Y, decimal flag, and cycle count; decrements S three times via reads.
    void reset();
    // Logical assertion (true means active); pins sampled once per clock.
    void set_irq(bool asserted) noexcept;
    void set_nmi(bool asserted) noexcept;
    [[nodiscard]] CpuState state() const noexcept;
    [[nodiscard]] std::uint64_t cycles() const noexcept;
    [[nodiscard]] bool at_instruction_boundary() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace rse::cpu
