// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <array>
#include <cstdint>

namespace rse::cpu {

// Logical accesses at instruction boundaries. The system owns address decoding,
// storage and wait states. Implementations return little-endian numeric values.
class Arm7Bus {
public:
    virtual ~Arm7Bus() = default;
    virtual std::uint32_t fetch_arm(std::uint32_t address) = 0;
    virtual std::uint16_t fetch_thumb(std::uint32_t address) = 0;
    virtual void store_word(std::uint32_t aligned_address, std::uint32_t value) = 0;
};

struct Arm7State {
    // R0 through R14 and the next instruction address at the step boundary.
    std::array<std::uint32_t, 15> registers{};
    std::uint32_t pc{};
    std::uint32_t cpsr{0xd3}; // Supervisor, IRQ/FIQ masked, ARM state.
    bool thumb() const noexcept { return (cpsr & (1u << 5)) != 0; }
};

// Initial instruction subset for ARM/Thumb interworking. The bus must outlive
// the CPU. Each step executes one instruction. Unsupported encodings throw
// std::runtime_error with PC and instruction and leave register state intact.
class Arm7Tdmi {
public:
    explicit Arm7Tdmi(Arm7Bus& bus) : bus_(bus) {}
    const Arm7State& state() const noexcept { return state_; }
    void reset() noexcept { state_ = {}; }
    void step();

private:
    void execute_arm(std::uint32_t instruction);
    void execute_thumb(std::uint16_t instruction);
    void branch_exchange(std::uint32_t target);
    Arm7Bus& bus_;
    Arm7State state_{};
};

}
