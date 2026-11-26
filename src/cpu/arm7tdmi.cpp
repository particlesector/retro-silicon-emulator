// SPDX-License-Identifier: Apache-2.0
#include <rse/cpu/arm7tdmi.hpp>
#include <bit>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace rse::cpu {
namespace {
using Word = std::uint32_t;
constexpr Word N = 1u << 31, Z = 1u << 30, C = 1u << 29, V = 1u << 28;
constexpr Word thumb_bit = 0x20, irq_mask = 0x80;
constexpr Word user = 0x10, irq = 0x12, supervisor = 0x13, system = 0x1f;

[[noreturn]] void unsupported(const Arm7State& state, Word instruction) {
    std::ostringstream message;
    message << "Unsupported " << (state.thumb() ? "Thumb" : "ARM")
            << " instruction 0x" << std::hex << instruction << " at 0x" << state.pc;
    throw std::runtime_error(message.str());
}

Arm7State::Bank& bank(Arm7State& state, Word status) {
    switch (status & 31) {
    case user: case system: return state.user_system;
    case irq: return state.irq;
    case supervisor: return state.supervisor;
    default: throw std::runtime_error("Unsupported ARM7 processor mode");
    }
}

bool has_saved_status(Word status) {
    return (status & 31) == irq || (status & 31) == supervisor;
}

bool condition(unsigned code, Word status) {
    const bool n = status & N, z = status & Z, c = status & C, v = status & V;
    switch (code) {
    case 0: return z;           case 1: return !z;
    case 2: return c;           case 3: return !c;
    case 4: return n;           case 5: return !n;
    case 6: return v;           case 7: return !v;
    case 8: return c && !z;     case 9: return !c || z;
    case 10: return n == v;     case 11: return n != v;
    case 12: return !z && n == v; case 13: return z || n != v;
    case 14: return true;       default: return false;
    }
}

struct Shift { Word value; bool carry; };

Shift shift(Word value, unsigned kind, unsigned count, bool carry, bool immediate) {
    if (!count) {
        if (!immediate || kind == 0) return {value, carry};
        if (kind == 3) return {(Word(carry) << 31) | (value >> 1), bool(value & 1)};
        count = 32; // Immediate LSR/ASR encode #32 as zero.
    }
    switch (kind) {
    case 0: // LSL
        return {count < 32 ? value << count : 0,
                count <= 32 && bool((value >> (32 - count)) & 1)};
    case 1: // LSR
        return {count < 32 ? value >> count : 0,
                count <= 32 && bool((value >> (count - 1)) & 1)};
    case 2: { // ASR
        const unsigned bits = count < 32 ? count : 31;
        const Word result = static_cast<Word>(static_cast<std::int32_t>(value) >> bits);
        return {result, count >= 32 ? bool(value & N) : bool((value >> (count - 1)) & 1)};
    }
    default: {
        const Word result = std::rotr(value, static_cast<int>(count % 32));
        return {result, bool(result & N)};
    }
    }
}

struct Result { Word value, flags; };

Word nz(Word value) { return (value & N) | (value == 0 ? Z : 0); }

Result add(Word left, Word right, bool carry) {
    const std::uint64_t sum = std::uint64_t{left} + right + carry;
    const Word value = static_cast<Word>(sum);
    const bool overflow = (~(left ^ right) & (left ^ value) & N) != 0;
    return {value, nz(value) | (sum >> 32 ? C : 0) | (overflow ? V : 0)};
}

// ARM data-processing op numbers. Subtraction uses carry as "no borrow".
Result alu(unsigned op, Word left, Shift right, Word status) {
    Word value{};
    switch (op) {
    case 0: case 8: value = left & right.value; break;
    case 1: case 9: value = left ^ right.value; break;
    case 2: case 10: return add(left, ~right.value, true);
    case 3: return add(right.value, ~left, true);
    case 4: case 11: return add(left, right.value, false);
    case 5: return add(left, right.value, status & C);
    case 6: return add(left, ~right.value, status & C);
    case 7: return add(right.value, ~left, status & C);
    case 12: value = left | right.value; break;
    case 13: value = right.value; break;
    case 14: value = left & ~right.value; break;
    default: value = ~right.value; break;
    }
    return {value, nz(value) | (right.carry ? C : 0) | (status & V)};
}

Word signed_offset(Word field, unsigned bits, unsigned scale) {
    const Word sign = 1u << (bits - 1);
    return ((field ^ sign) - sign) * scale;
}
}

void Arm7Tdmi::run(std::uint64_t steps) {
    for (; steps != 0; --steps) step();
}

void Arm7Tdmi::step() {
    if (state_.irq_line && !(state_.cpsr & irq_mask)) enter_exception(true);
    else if (state_.thumb()) execute_thumb(bus_.fetch_thumb(state_.pc));
    else execute_arm(bus_.fetch_arm(state_.pc));
}

Word Arm7Tdmi::operand(unsigned reg) const {
    return reg == 15 ? state_.pc + (state_.thumb() ? 4 : 8) : state_.registers[reg];
}

void Arm7Tdmi::change_status(Word status) {
    auto& previous = bank(state_, state_.cpsr);
    auto& next = bank(state_, status);
    if (&previous != &next) {
        std::swap(previous.sp, state_.registers[13]);
        std::swap(previous.lr, state_.registers[14]);
        std::swap(next.sp, state_.registers[13]);
        std::swap(next.lr, state_.registers[14]);
    }
    state_.cpsr = status;
}

void Arm7Tdmi::enter_exception(bool is_irq) {
    const Word saved = state_.cpsr;
    const Word link = state_.pc + (is_irq || !state_.thumb() ? 4 : 2);
    change_status((saved & ~(thumb_bit | 31u)) | irq_mask | (is_irq ? irq : supervisor));
    bank(state_, state_.cpsr).spsr = saved;
    state_.registers[14] = link;
    branch_to(is_irq ? 0x18 : 0x08);
}

void Arm7Tdmi::branch_exchange(Word target) {
    state_.cpsr = (state_.cpsr & ~thumb_bit) | ((target & 1) ? thumb_bit : 0);
    branch_to(target);
}

void Arm7Tdmi::branch_to(Word target) {
    state_.pc = target & (state_.thumb() ? ~1u : ~3u);
    bus_.branch();
}

void Arm7Tdmi::data_processing(Word instruction) {
    const unsigned op = (instruction >> 21) & 15;
    const unsigned rn = (instruction >> 16) & 15, rd = (instruction >> 12) & 15;
    const bool test = op >= 8 && op <= 11;
    const bool flags = instruction & (1u << 20);
    if (test && !flags) unsupported(state_, instruction);

    Shift right{};
    Word left = operand(rn);
    if (instruction & (1u << 25)) {
        const int rotation = 2 * ((instruction >> 8) & 15);
        right.value = std::rotr(instruction & 255, rotation);
        right.carry = rotation ? bool(right.value & N) : bool(state_.cpsr & C);
    } else {
        const unsigned rm = instruction & 15;
        const bool by_register = instruction & 16;
        Word value = operand(rm);
        unsigned count = (instruction >> 7) & 31;
        if (by_register) {
            const unsigned rs = (instruction >> 8) & 15;
            if ((instruction & 0x80) || rs == 15) unsupported(state_, instruction);
            count = operand(rs) & 255;
            if (rm == 15) value += 4;
            if (rn == 15) left += 4;
        }
        right = shift(value, (instruction >> 5) & 3, count, state_.cpsr & C, !by_register);
    }

    const Result result = alu(op, left, right, state_.cpsr);
    if (!test && rd == 15) {
        if (flags) {
            if (!has_saved_status(state_.cpsr)) unsupported(state_, instruction);
            change_status(bank(state_, state_.cpsr).spsr);
        }
        branch_to(result.value);
        return;
    }
    if (!test) state_.registers[rd] = result.value;
    if (flags) state_.cpsr = (state_.cpsr & 0x0fffffff) | result.flags;
    state_.pc += 4;
}

void Arm7Tdmi::transfer(Word address, unsigned reg, unsigned width, bool load, bool sign_extend) {
    Word value{};
    if (load) {
        if (width == 4) value = std::rotr(bus_.load_word(address & ~3u), int(8 * (address & 3)));
        else if (width == 2 && !(sign_extend && (address & 1))) {
            value = bus_.load_half(address & ~1u);
            if (sign_extend) value = signed_offset(value, 16, 1);
            else if (address & 1) value = std::rotr(value, 8);
        } else {
            value = bus_.load_byte(address);
            if (sign_extend) value = signed_offset(value, 8, 1);
        }
        if (reg == 15) {
            branch_to(value);
            return;
        }
        state_.registers[reg] = value;
    } else {
        value = reg == 15 ? state_.pc + 12 : state_.registers[reg];
        if (width == 4) bus_.store_word(address & ~3u, value);
        else if (width == 2) bus_.store_half(address & ~1u, static_cast<std::uint16_t>(value));
        else bus_.store_byte(address, static_cast<std::uint8_t>(value));
    }
    state_.pc += state_.thumb() ? 2 : 4;
}

void Arm7Tdmi::transfer_multiple(Word address, Word registers, bool load) {
    address &= ~3u;
    for (unsigned reg = 0; reg < 16; ++reg) {
        if (!(registers & (1u << reg))) continue;
        if (load) {
            const auto value = bus_.load_word(address);
            if (reg == 15) branch_to(value);
            else state_.registers[reg] = value;
        } else {
            bus_.store_word(address, reg == 15 ? state_.pc + 12 : state_.registers[reg]);
        }
        address += 4;
    }
    if (!load || !(registers & 0x8000)) state_.pc += state_.thumb() ? 2 : 4;
}

void Arm7Tdmi::execute_arm(Word instruction) {
    if (!condition(instruction >> 28, state_.cpsr)) {
        state_.pc += 4;
        return;
    }
    if ((instruction & 0x0ffffff0) == 0x012fff10) {
        branch_exchange(operand(instruction & 15));
        return;
    }
    if ((instruction & 0x0f000000) == 0x0f000000) {
        enter_exception(false);
        return;
    }
    if ((instruction & 0x0e000000) == 0x0a000000) { // B, BL
        if (instruction & (1u << 24)) state_.registers[14] = state_.pc + 4;
        branch_to(state_.pc + 8 + signed_offset(instruction & 0xffffff, 24, 4));
        return;
    }
    if ((instruction & 0x0e000000) == 0x08000000) { // LDM, STM
        const unsigned rn = (instruction >> 16) & 15;
        const Word registers = instruction & 0xffff;
        const bool load = instruction & 0x00100000, write_back = instruction & 0x00200000;
        const bool up = instruction & 0x00800000, pre = instruction & 0x01000000;
        if (!registers || rn == 15 || (instruction & 0x00400000) ||
            (!load && write_back && (registers & (1u << rn)) && (registers & ((1u << rn) - 1))))
            unsupported(state_, instruction);
        const Word base = operand(rn);
        const Word size = 4 * std::popcount(registers);
        const Word start = up ? base + (pre ? 4 : 0) : base - size + (pre ? 0 : 4);
        transfer_multiple(start, registers, load);
        if (write_back && (!load || !(registers & (1u << rn))))
            state_.registers[rn] = up ? base + size : base - size;
        return;
    }
    const unsigned rd = (instruction >> 12) & 15;
    const bool saved = instruction & (1u << 22);
    if ((instruction & 0x0fbf0fff) == 0x010f0000) { // MRS
        if (rd == 15 || (saved && !has_saved_status(state_.cpsr))) unsupported(state_, instruction);
        state_.registers[rd] = saved ? bank(state_, state_.cpsr).spsr : state_.cpsr;
        state_.pc += 4;
        return;
    }
    if ((instruction & 0x0fb0fff0) == 0x0120f000 ||
        (instruction & 0x0fb0f000) == 0x0320f000) { // MSR
        if (saved && !has_saved_status(state_.cpsr)) unsupported(state_, instruction);
        const Word value = instruction & (1u << 25)
            ? std::rotr(instruction & 255, int(2 * ((instruction >> 8) & 15)))
            : operand(instruction & 15);
        Word mask = instruction & (1u << 19) ? 0xf0000000 : 0;
        if ((instruction & (1u << 16)) && (state_.cpsr & 31) != user)
            mask |= saved ? 0xff : (0xff & ~thumb_bit);
        if (saved) {
            auto& spsr = bank(state_, state_.cpsr).spsr;
            spsr = (spsr & ~mask) | (value & mask);
        } else {
            change_status((state_.cpsr & ~mask) | (value & mask));
        }
        state_.pc += 4;
        return;
    }
    if ((instruction & 0x0f8000f0) == 0x00800090) { // Signed/unsigned long multiply
        const unsigned high = (instruction >> 16) & 15, rs = (instruction >> 8) & 15;
        const unsigned rm = instruction & 15;
        if (high == 15 || rd == 15 || rs == 15 || rm == 15 || high == rd || high == rm || rd == rm)
            unsupported(state_, instruction);
        const auto left = operand(rm), right = operand(rs);
        auto result = instruction & 0x00400000
            ? std::uint64_t(std::int64_t(std::int32_t(left)) * std::int32_t(right))
            : std::uint64_t(left) * right;
        if (instruction & 0x00200000) result += (std::uint64_t(operand(high)) << 32) | operand(rd);
        state_.registers[high] = Word(result >> 32);
        state_.registers[rd] = Word(result);
        if (instruction & 0x00100000)
            state_.cpsr = (state_.cpsr & ~(N | Z)) | (Word(result >> 32) & N) | (result == 0 ? Z : 0);
        state_.pc += 4;
        return;
    }
    if ((instruction & 0x0fc000f0) == 0x00000090) { // MUL, MLA
        const unsigned dest = (instruction >> 16) & 15, rs = (instruction >> 8) & 15;
        const unsigned rm = instruction & 15;
        const bool accumulate = instruction & 0x00200000;
        if (dest == 15 || rs == 15 || rm == 15 || dest == rm ||
            (accumulate ? rd == 15 : rd != 0))
            unsupported(state_, instruction);
        const Word result = operand(rm) * operand(rs) + (accumulate ? operand(rd) : 0);
        state_.registers[dest] = result;
        if (instruction & 0x00100000) state_.cpsr = (state_.cpsr & ~(N | Z)) | nz(result);
        state_.pc += 4;
        return;
    }

    const bool single = (instruction & 0x0c000000) == 0x04000000;
    const bool half = (instruction & 0x0e000090) == 0x00000090 && (instruction & 0x60);
    if (single || half) {
        const unsigned rn = (instruction >> 16) & 15;
        const bool pre = instruction & (1u << 24), load = instruction & (1u << 20);
        const bool write_back = !pre || (instruction & (1u << 21));
        const bool sign_extend = half && (instruction & 0x40);
        const unsigned width = half ? (instruction & 0x20 ? 2 : 1) : (instruction & (1u << 22) ? 1 : 4);
        if ((sign_extend && !load) || (write_back && (rn == 15 || (load && rn == rd))) || (rd == 15 && width != 4))
            unsupported(state_, instruction);
        Word offset = instruction & 0xfff;
        if (half) {
            offset = instruction & (1u << 22)
                ? ((instruction >> 4) & 0xf0) | (instruction & 15)
                : operand(instruction & 15);
        } else if (instruction & (1u << 25)) {
            if (instruction & 16) unsupported(state_, instruction);
            offset = shift(operand(instruction & 15), (instruction >> 5) & 3,
                           (instruction >> 7) & 31, state_.cpsr & C, true).value;
        }
        const Word base = operand(rn);
        const Word indexed = instruction & (1u << 23) ? base + offset : base - offset;
        transfer(pre ? indexed : base, rd, width, load, sign_extend);
        if (write_back) state_.registers[rn] = indexed;
        return;
    }
    if ((instruction & 0x0c000000) == 0) {
        data_processing(instruction);
        return;
    }
    unsupported(state_, instruction);
}

void Arm7Tdmi::execute_thumb(std::uint16_t instruction) {
    const unsigned rd = instruction & 7, rs = (instruction >> 3) & 7;
    auto commit = [&](unsigned dest, Result result, bool write = true) {
        if (write) state_.registers[dest] = result.value;
        state_.cpsr = (state_.cpsr & 0x0fffffff) | result.flags;
        state_.pc += 2;
    };
    if ((instruction & 0xf800) == 0x1800) { // Three-operand ADD/SUB
        const unsigned field = (instruction >> 6) & 7;
        const Word right = instruction & 0x400 ? field : operand(field);
        commit(rd, alu(instruction & 0x200 ? 2 : 4, operand(rs), {right, false}, state_.cpsr));
    } else if ((instruction & 0xe000) == 0) { // Immediate LSL/LSR/ASR
        auto result = shift(operand(rs), instruction >> 11, (instruction >> 6) & 31, state_.cpsr & C, true);
        commit(rd, alu(13, 0, result, state_.cpsr));
    } else if ((instruction & 0xe000) == 0x2000) { // MOV/CMP/ADD/SUB imm8
        constexpr unsigned ops[] = {13, 10, 4, 2};
        const unsigned dest = (instruction >> 8) & 7, op = ops[(instruction >> 11) & 3];
        commit(dest, alu(op, operand(dest), {instruction & 255u, bool(state_.cpsr & C)}, state_.cpsr), op != 10);
    } else if ((instruction & 0xfc00) == 0x4000) { // Register ALU
        const unsigned op = (instruction >> 6) & 15;
        const Word left = operand(rd), right = operand(rs);
        if ((op >= 2 && op <= 4) || op == 7) {
            auto result = shift(left, op == 7 ? 3 : op - 2, right & 255, state_.cpsr & C, false);
            commit(rd, alu(13, 0, result, state_.cpsr));
        } else if (op == 13) {
            commit(rd, {left * right, nz(left * right) | (state_.cpsr & (C | V))});
        } else {
            constexpr unsigned ops[] = {0,1,0,0,0,5,6,0,8,2,10,11,12,0,14,15};
            const Word first = op == 9 ? 0 : left; // NEG = 0 - Rs
            commit(rd, alu(ops[op], first, {right, bool(state_.cpsr & C)}, state_.cpsr),
                   op != 8 && op != 10 && op != 11);
        }
    } else if ((instruction & 0xfc00) == 0x4400) { // High registers and BX
        const unsigned dest = rd | ((instruction >> 4) & 8);
        const Word source = operand((instruction >> 3) & 15);
        switch ((instruction >> 8) & 3) {
        case 3:
            if (instruction & 0x87) unsupported(state_, instruction);
            branch_exchange(source);
            return;
        case 1: commit(dest, add(operand(dest), ~source, true), false); return;
        default:
            const Word value = instruction & 0x200 ? source : operand(dest) + source;
            if (dest == 15) branch_to(value);
            else { state_.registers[dest] = value; state_.pc += 2; }
        }
    } else if ((instruction & 0xf800) == 0x4800) { // PC-relative LDR
        transfer(((state_.pc + 4) & ~3u) + 4u * (instruction & 255), (instruction >> 8) & 7, 4, true);
    } else if ((instruction & 0xf000) == 0x5000) { // Register-offset transfers
        const unsigned op = (instruction >> 9) & 7;
        constexpr unsigned widths[] = {4, 2, 1, 1, 4, 2, 1, 2};
        transfer(operand(rs) + operand((instruction >> 6) & 7), rd, widths[op],
                 op >= 3, op == 3 || op == 7);
    } else if ((instruction & 0xe000) == 0x6000 || (instruction & 0xf000) == 0x8000) {
        const unsigned width = instruction & 0x8000 ? 2 : (instruction & 0x1000 ? 1 : 4);
        transfer(operand(rs) + width * ((instruction >> 6) & 31), rd, width, instruction & 0x800);
    } else if ((instruction & 0xf000) == 0x9000) { // SP-relative LDR/STR
        transfer(operand(13) + 4u * (instruction & 255), (instruction >> 8) & 7, 4, instruction & 0x800);
    } else if ((instruction & 0xf000) == 0xa000) { // Form PC/SP-relative address
        const Word base = instruction & 0x800 ? operand(13) : operand(15) & ~3u;
        state_.registers[(instruction >> 8) & 7] = base + 4u * (instruction & 255);
        state_.pc += 2;
    } else if ((instruction & 0xff00) == 0xb000) { // Adjust SP
        const Word offset = 4u * (instruction & 127);
        state_.registers[13] += instruction & 128 ? -offset : offset;
        state_.pc += 2;
    } else if ((instruction & 0xf600) == 0xb400) { // PUSH, POP
        const bool load = instruction & 0x800;
        Word registers = instruction & 255;
        if (instruction & 0x100) registers |= 1u << (load ? 15 : 14);
        if (!registers) unsupported(state_, instruction);
        const Word size = 4 * std::popcount(registers), sp = operand(13);
        transfer_multiple(load ? sp : sp - size, registers, load);
        state_.registers[13] = load ? sp + size : sp - size;
    } else if ((instruction & 0xf000) == 0xc000) { // LDMIA, STMIA
        const bool load = instruction & 0x800;
        const unsigned base_reg = (instruction >> 8) & 7;
        const Word registers = instruction & 255, base = operand(base_reg);
        if (!registers || (!load && (registers & (1u << base_reg)) && (registers & ((1u << base_reg) - 1))))
            unsupported(state_, instruction);
        transfer_multiple(base, registers, load);
        if (!load || !(registers & (1u << base_reg))) state_.registers[base_reg] = base + 4 * std::popcount(registers);
    } else if ((instruction & 0xff00) == 0xdf00) {
        enter_exception(false);
    } else if ((instruction & 0xf000) == 0xd000) {
        const unsigned cond = (instruction >> 8) & 15;
        if (cond == 14) unsupported(state_, instruction);
        if (condition(cond, state_.cpsr)) branch_to(state_.pc + 4 + signed_offset(instruction & 255, 8, 2));
        else state_.pc += 2;
    } else if ((instruction & 0xf800) == 0xe000) {
        branch_to(state_.pc + 4 + signed_offset(instruction & 0x7ff, 11, 2));
    } else if ((instruction & 0xf800) == 0xf000) { // BL prefix
        state_.registers[14] = state_.pc + 4 + signed_offset(instruction & 0x7ff, 11, 4096);
        state_.pc += 2;
    } else if ((instruction & 0xf800) == 0xf800) { // BL suffix
        const Word target = operand(14) + 2u * (instruction & 0x7ff);
        state_.registers[14] = (state_.pc + 2) | 1;
        branch_to(target);
    } else {
        unsupported(state_, instruction);
    }
}

}
