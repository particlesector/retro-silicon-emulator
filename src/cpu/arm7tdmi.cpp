// SPDX-License-Identifier: Apache-2.0
#include <rse/cpu/arm7tdmi.hpp>
#include <bit>
#include <sstream>
#include <stdexcept>

namespace rse::cpu {
namespace {
[[noreturn]] void unsupported(const Arm7State& state, std::uint32_t instruction) {
    std::ostringstream message;
    message << "Unsupported " << (state.thumb() ? "Thumb" : "ARM")
            << " instruction 0x" << std::hex << instruction << " at 0x" << state.pc;
    throw std::runtime_error(message.str());
}
}

void Arm7Tdmi::step() {
    if (state_.thumb())
        execute_thumb(bus_.fetch_thumb(state_.pc));
    else
        execute_arm(bus_.fetch_arm(state_.pc));
}

void Arm7Tdmi::branch_exchange(std::uint32_t target) {
    constexpr auto thumb_bit = 1u << 5;
    if (target & 1u) {
        state_.cpsr |= thumb_bit;
        state_.pc = target & ~1u;
    } else {
        state_.cpsr &= ~thumb_bit;
        state_.pc = target & ~3u;
    }
}

void Arm7Tdmi::execute_arm(std::uint32_t instruction) {
    // AL MOV (immediate), without flag update. Operand2 rotates an 8-bit
    // constant right by twice the rotation field.
    if ((instruction & 0xffff0000u) == 0xe3a00000u) {
        const unsigned destination = (instruction >> 12) & 15;
        const int rotation = 2 * static_cast<int>((instruction >> 8) & 15);
        const auto value = std::rotr(instruction & 0xffu, rotation);
        if (destination == 15) {
            state_.pc = value & ~3u;
        } else {
            state_.registers[destination] = value;
            state_.pc += 4;
        }
        return;
    }
    if ((instruction & 0xfffffff0u) == 0xe12fff10u) { // AL BX Rm
        const unsigned source = instruction & 15;
        branch_exchange(source == 15 ? state_.pc + 8 : state_.registers[source]);
        return;
    }
    unsupported(state_, instruction);
}

void Arm7Tdmi::execute_thumb(std::uint16_t instruction) {
    if ((instruction & 0xf800) == 0x2000) { // MOVS Rd, #imm8
        const auto value = instruction & 0xffu;
        state_.registers[(instruction >> 8) & 7] = value;
        // A zero-extended byte always clears N. C and V are preserved.
        state_.cpsr = (state_.cpsr & 0x3fffffffu) | (value == 0 ? 1u << 30 : 0u);
    } else if ((instruction & 0xf800) == 0x6000) { // STR Rd, [Rb, #imm5 * 4]
        const auto base = state_.registers[(instruction >> 3) & 7];
        const auto offset = 4u * ((instruction >> 6) & 31);
        bus_.store_word((base + offset) & ~3u, state_.registers[instruction & 7]);
    } else if ((instruction & 0xff87) == 0x4700) { // BX Rm
        const unsigned source = (instruction >> 3) & 15;
        branch_exchange(source == 15 ? state_.pc + 4 : state_.registers[source]);
        return;
    } else {
        unsupported(state_, instruction);
    }
    state_.pc += 2;
}

}
