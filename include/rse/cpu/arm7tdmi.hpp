// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <array>
#include <cstdint>

namespace rse::cpu {

// Logical accesses at instruction boundaries. The system owns address decoding,
// storage and wait states. Implementations return little-endian numeric values.
class Arm7Bus {
public:
    // A clocked bus can suspend an access and remember earlier accesses in the
    // instruction. On retry it must return their original values and avoid
    // repeating their side effects.
    struct Stall {};
    virtual ~Arm7Bus() = default;
    virtual std::uint32_t fetch_arm(std::uint32_t address) = 0;
    virtual std::uint16_t fetch_thumb(std::uint32_t address) = 0;
    virtual void store_word(std::uint32_t aligned_address, std::uint32_t value) = 0;
    virtual std::uint32_t load_word(std::uint32_t aligned_address) = 0;
    virtual std::uint16_t load_half(std::uint32_t aligned_address) = 0;
    virtual std::uint8_t load_byte(std::uint32_t address) = 0;
    virtual void store_half(std::uint32_t aligned_address, std::uint16_t value) = 0;
    virtual void store_byte(std::uint32_t address, std::uint8_t value) = 0;
    // A taken branch, exception or other explicit PC write ends the fetch burst.
    virtual void branch() {}
};

struct Arm7State {
    // R0 through R14 and the next instruction address at the step boundary.
    std::array<std::uint32_t, 15> registers{};
    std::uint32_t pc{};
    std::uint32_t cpsr{0xd3}; // Supervisor, IRQ/FIQ masked, ARM state.
    struct Bank {
        // Dormant SP/LR while another bank is active. Active values are in registers.
        std::uint32_t sp{}, lr{}, spsr{};
        bool operator==(const Bank&) const = default;
    };
    Bank user_system{}, supervisor{}, irq{};
    bool irq_line{};
    bool operator==(const Arm7State&) const = default;
    bool thumb() const noexcept { return (cpsr & (1u << 5)) != 0; }
};

// Instruction-boundary core. The bus must outlive the CPU. A step accepts a
// pending unmasked IRQ or executes one instruction. Unsupported encodings throw
// std::runtime_error with PC and instruction and leave register state intact.
class Arm7Tdmi {
public:
    explicit Arm7Tdmi(Arm7Bus& bus) : bus_(bus) {}
    const Arm7State& state() const noexcept { return state_; }
    void reset() noexcept { state_ = {}; }
    void step();
    // Restore the instruction-boundary registers if the bus suspends the step.
    [[nodiscard]] bool step_with_stalls();
    // Counts step boundaries, including IRQ entry. Zero performs no work.
    void run(std::uint64_t steps);
    // Level sampled before fetch. Taking IRQ leaves the external level asserted.
    void set_irq(bool asserted) noexcept { state_.irq_line = asserted; }

private:
    void execute_arm(std::uint32_t instruction);
    void execute_thumb(std::uint16_t instruction);
    void branch_exchange(std::uint32_t target);
    void branch_to(std::uint32_t target);
    std::uint32_t operand(unsigned reg) const;
    void change_status(std::uint32_t status);
    void enter_exception(bool irq);
    void data_processing(std::uint32_t instruction);
    void transfer(std::uint32_t address, unsigned reg, unsigned width, bool load, bool sign_extend = false);
    void transfer_multiple(std::uint32_t address, std::uint32_t registers, bool load);
    Arm7Bus& bus_;
    Arm7State state_{};
};

}
