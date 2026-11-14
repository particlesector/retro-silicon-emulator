// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <rse/cpu/mos6502.hpp>
#include <rse/systems/nes/cpu_bus.hpp>

namespace rse::nes {
using CpuState = cpu::CpuState;
using UnsupportedOpcode = cpu::UnsupportedOpcode;
// Thin NES adapter: only selects the Ricoh arithmetic variant. APU, DMA, memory
// mapping, and clock-domain conversion remain separate NES system devices.
class Cpu final {
public:
    explicit Cpu(CpuBus& bus) : core_(bus, cpu::Variant::ricoh2a03) {}
    Cpu(CpuBus& bus, CpuState initial) : core_(bus, initial, cpu::Variant::ricoh2a03) {}
    void clock() { core_.clock(); }
    void reset() { core_.reset(); }
    void set_irq(bool asserted) noexcept { core_.set_irq(asserted); }
    void set_nmi(bool asserted) noexcept { core_.set_nmi(asserted); }
    [[nodiscard]] CpuState state() const noexcept { return core_.state(); }
    [[nodiscard]] std::uint64_t cycles() const noexcept { return core_.cycles(); }
    [[nodiscard]] bool at_instruction_boundary() const noexcept { return core_.at_instruction_boundary(); }
private:
    cpu::Mos6502 core_;
};
} // namespace rse::nes
