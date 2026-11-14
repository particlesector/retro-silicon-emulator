// SPDX-License-Identifier: Apache-2.0
#include <rse/cpu/mos6502.hpp>
#include <array>
#include <coroutine>
#include <exception>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace rse::cpu {
namespace {
constexpr std::uint8_t C = 1, Z = 2, I = 4, D = 8, B = 16, U = 32, V = 64, N = 128;
enum class Op {
    invalid, ORA, AND, EOR, ADC, STA, LDA, CMP, SBC, ASL, ROL, LSR, ROR,
    STX, LDX, DEC, INC, BIT, STY, LDY, CPY, CPX,
    BRK, PHP, BPL, CLC, JSR, PLP, BMI, SEC, RTI, PHA, JMP, BVC, CLI,
    RTS, PLA, BVS, SEI, DEY, TXA, BCC, TYA, TXS, TAY, TAX, BCS,
    CLV, TSX, INY, DEX, BNE, CLD, INX, NOP, BEQ, SED
};
enum class Mode { imp, acc, imm, zp, zpx, zpy, abs, abx, aby, ind, izx, izy, rel };
struct Instruction { Op op{}; Mode mode{}; };
constexpr auto instructions = [] {
    std::array<Instruction, 256> table{};
#define INS(code, op, mode) table[0x##code] = {Op::op, Mode::mode}
    INS(00,BRK,imp); INS(01,ORA,izx); INS(05,ORA,zp); INS(06,ASL,zp);
    INS(08,PHP,imp); INS(09,ORA,imm); INS(0a,ASL,acc); INS(0d,ORA,abs); INS(0e,ASL,abs);
    INS(10,BPL,rel); INS(11,ORA,izy); INS(15,ORA,zpx); INS(16,ASL,zpx);
    INS(18,CLC,imp); INS(19,ORA,aby); INS(1d,ORA,abx); INS(1e,ASL,abx);
    INS(20,JSR,abs); INS(21,AND,izx); INS(24,BIT,zp); INS(25,AND,zp); INS(26,ROL,zp);
    INS(28,PLP,imp); INS(29,AND,imm); INS(2a,ROL,acc); INS(2c,BIT,abs); INS(2d,AND,abs); INS(2e,ROL,abs);
    INS(30,BMI,rel); INS(31,AND,izy); INS(35,AND,zpx); INS(36,ROL,zpx);
    INS(38,SEC,imp); INS(39,AND,aby); INS(3d,AND,abx); INS(3e,ROL,abx);
    INS(40,RTI,imp); INS(41,EOR,izx); INS(45,EOR,zp); INS(46,LSR,zp);
    INS(48,PHA,imp); INS(49,EOR,imm); INS(4a,LSR,acc); INS(4c,JMP,abs); INS(4d,EOR,abs); INS(4e,LSR,abs);
    INS(50,BVC,rel); INS(51,EOR,izy); INS(55,EOR,zpx); INS(56,LSR,zpx);
    INS(58,CLI,imp); INS(59,EOR,aby); INS(5d,EOR,abx); INS(5e,LSR,abx);
    INS(60,RTS,imp); INS(61,ADC,izx); INS(65,ADC,zp); INS(66,ROR,zp);
    INS(68,PLA,imp); INS(69,ADC,imm); INS(6a,ROR,acc); INS(6c,JMP,ind); INS(6d,ADC,abs); INS(6e,ROR,abs);
    INS(70,BVS,rel); INS(71,ADC,izy); INS(75,ADC,zpx); INS(76,ROR,zpx);
    INS(78,SEI,imp); INS(79,ADC,aby); INS(7d,ADC,abx); INS(7e,ROR,abx);
    INS(81,STA,izx); INS(84,STY,zp); INS(85,STA,zp); INS(86,STX,zp);
    INS(88,DEY,imp); INS(8a,TXA,imp); INS(8c,STY,abs); INS(8d,STA,abs); INS(8e,STX,abs);
    INS(90,BCC,rel); INS(91,STA,izy); INS(94,STY,zpx); INS(95,STA,zpx); INS(96,STX,zpy);
    INS(98,TYA,imp); INS(99,STA,aby); INS(9a,TXS,imp); INS(9d,STA,abx);
    INS(a0,LDY,imm); INS(a1,LDA,izx); INS(a2,LDX,imm); INS(a4,LDY,zp); INS(a5,LDA,zp); INS(a6,LDX,zp);
    INS(a8,TAY,imp); INS(a9,LDA,imm); INS(aa,TAX,imp); INS(ac,LDY,abs); INS(ad,LDA,abs); INS(ae,LDX,abs);
    INS(b0,BCS,rel); INS(b1,LDA,izy); INS(b4,LDY,zpx); INS(b5,LDA,zpx); INS(b6,LDX,zpy);
    INS(b8,CLV,imp); INS(b9,LDA,aby); INS(ba,TSX,imp); INS(bc,LDY,abx); INS(bd,LDA,abx); INS(be,LDX,aby);
    INS(c0,CPY,imm); INS(c1,CMP,izx); INS(c4,CPY,zp); INS(c5,CMP,zp); INS(c6,DEC,zp);
    INS(c8,INY,imp); INS(c9,CMP,imm); INS(ca,DEX,imp); INS(cc,CPY,abs); INS(cd,CMP,abs); INS(ce,DEC,abs);
    INS(d0,BNE,rel); INS(d1,CMP,izy); INS(d5,CMP,zpx); INS(d6,DEC,zpx);
    INS(d8,CLD,imp); INS(d9,CMP,aby); INS(dd,CMP,abx); INS(de,DEC,abx);
    INS(e0,CPX,imm); INS(e1,SBC,izx); INS(e4,CPX,zp); INS(e5,SBC,zp); INS(e6,INC,zp);
    INS(e8,INX,imp); INS(e9,SBC,imm); INS(ea,NOP,imp); INS(ec,CPX,abs); INS(ed,SBC,abs); INS(ee,INC,abs);
    INS(f0,BEQ,rel); INS(f1,SBC,izy); INS(f5,SBC,zpx); INS(f6,INC,zpx);
    INS(f8,SED,imp); INS(f9,SBC,aby); INS(fd,SBC,abx); INS(fe,INC,abx);
#undef INS
    return table;
}();
constexpr bool modifies(Op op) {
    return op == Op::ASL || op == Op::LSR || op == Op::ROL || op == Op::ROR || op == Op::INC || op == Op::DEC;
}
constexpr bool stores(Op op) { return op == Op::STA || op == Op::STX || op == Op::STY; }
std::string opcode_message(std::uint8_t opcode, std::uint16_t address) {
    std::ostringstream text;
    text << "unsupported opcode $" << std::hex << std::setfill('0') << std::setw(2)
         << unsigned(opcode) << " at $" << std::setw(4) << address;
    return text.str();
}
}

UnsupportedOpcode::UnsupportedOpcode(std::uint8_t code, std::uint16_t pc)
    : std::runtime_error(opcode_message(code, pc)), opcode(code), address(pc) {}

struct Mos6502::Impl {
    struct Task {
        struct promise_type {
            std::exception_ptr error;
            Task get_return_object() { return Task{std::coroutine_handle<promise_type>::from_promise(*this)}; }
            std::suspend_always initial_suspend() noexcept { return {}; }
            std::suspend_always final_suspend() noexcept { return {}; }
            void return_void() noexcept {}
            void unhandled_exception() noexcept { error = std::current_exception(); }
        };
        std::coroutine_handle<promise_type> handle{};
        explicit Task(std::coroutine_handle<promise_type> h = {}) : handle(h) {}
        Task(Task&& other) noexcept : handle(std::exchange(other.handle, {})) {}
        Task& operator=(Task&& other) noexcept {
            if (handle) handle.destroy();
            handle = std::exchange(other.handle, {});
            return *this;
        }
        ~Task() { if (handle) handle.destroy(); }
    };
    struct Io {
        Impl& cpu;
        std::uint16_t address;
        std::uint8_t value{};
        bool writing{};
        bool increment_pc{};
        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<>) const noexcept {
            cpu.address = address; cpu.value = value; cpu.writing = writing;
        }
        std::uint8_t await_resume() const noexcept {
            if (increment_pc) ++cpu.r.pc;
            return cpu.value;
        }
    };
    enum class Interrupt { none, irq, nmi };
    Bus& bus;
    Variant variant;
    CpuState r;
    std::uint64_t cycle{};
    bool irq{}, nmi{}, last_nmi{}, nmi_pending{}, reset_pending{}, faulted{};
    Interrupt pending{Interrupt::none};
    std::uint16_t address{};
    std::uint8_t value{};
    bool writing{};
    Task task;

    Impl(Bus& connection, CpuState initial, bool reset, Variant model)
        : bus(connection), variant(model), r(initial), reset_pending(reset) {
        if (model != Variant::nmos6502 && model != Variant::ricoh2a03)
            throw std::invalid_argument("unsupported 6502 variant");
        r.p = (r.p | U) & ~B;
    }
    Io read(std::uint16_t a) { return {*this, a}; }
    Io fetch() { return {*this, r.pc, 0, false, true}; }
    Io write(std::uint16_t a, std::uint8_t v) { return {*this, a, v, true}; }
    void flag(std::uint8_t mask, bool set) { r.p = set ? r.p | mask : r.p & ~mask; }
    void nz(std::uint8_t v) { flag(Z, v == 0); flag(N, (v & N) != 0); }
    void poll() {
        if (nmi_pending) pending = Interrupt::nmi;
        else if (irq && !(r.p & I) && pending == Interrupt::none) pending = Interrupt::irq;
    }
    void adc(std::uint8_t v) {
        const unsigned sum = r.a + v + (r.p & C);
        const auto result = static_cast<std::uint8_t>(sum);
        if (variant == Variant::nmos6502 && (r.p & D)) {
            unsigned low = (r.a & 15) + (v & 15) + (r.p & C);
            if (low >= 10) low = ((low + 6) & 15) + 16;
            unsigned adjusted = (r.a & 0xf0) + (v & 0xf0) + low;
            // NMOS N/V observe the intermediate high-nibble sum; Z observes
            // the binary sum, before decimal correction.
            flag(Z, result == 0);
            flag(N, adjusted & N);
            flag(V, (~(r.a ^ v) & (r.a ^ adjusted) & N) != 0);
            if (adjusted >= 0xa0) adjusted += 0x60;
            flag(C, adjusted > 255);
            r.a = static_cast<std::uint8_t>(adjusted);
            return;
        }
        flag(V, ((~(r.a ^ v) & (r.a ^ result)) & N) != 0);
        flag(C, sum > 255);
        r.a = result;
        nz(r.a);
    }
    void sbc(std::uint8_t v) {
        if (variant != Variant::nmos6502 || !(r.p & D)) {
            adc(static_cast<std::uint8_t>(~v));
            return;
        }
        const int borrow = (r.p & C) ? 0 : 1;
        const int difference = int(r.a) - v - borrow;
        const auto binary = static_cast<std::uint8_t>(difference);
        int low = int(r.a & 15) - (v & 15) - borrow;
        if (low < 0) low = ((low - 6) & 15) - 16;
        int adjusted = int(r.a & 0xf0) - (v & 0xf0) + low;
        if (adjusted < 0) adjusted -= 0x60;
        flag(V, ((r.a ^ v) & (r.a ^ binary) & N) != 0);
        flag(C, difference >= 0);
        nz(binary);
        r.a = static_cast<std::uint8_t>(adjusted);
    }
    void compare(std::uint8_t reg, std::uint8_t v) {
        flag(C, reg >= v); nz(static_cast<std::uint8_t>(reg - v));
    }
    std::uint8_t modify(Op op, std::uint8_t v) {
        const auto carry = r.p & C;
        switch (op) {
        case Op::ASL: flag(C, v & 0x80); v = static_cast<std::uint8_t>(v << 1); break;
        case Op::LSR: flag(C, v & 1); v >>= 1; break;
        case Op::ROL: flag(C, v & 0x80); v = static_cast<std::uint8_t>((v << 1) | carry); break;
        case Op::ROR: flag(C, v & 1); v = static_cast<std::uint8_t>((v >> 1) | (carry << 7)); break;
        case Op::INC: ++v; break;
        case Op::DEC: --v; break;
        default: throw std::logic_error("invalid modify operation");
        }
        nz(v);
        return v;
    }
    void apply(Op op, std::uint8_t v) {
        switch (op) {
        case Op::ORA: r.a |= v; nz(r.a); break;
        case Op::AND: r.a &= v; nz(r.a); break;
        case Op::EOR: r.a ^= v; nz(r.a); break;
        case Op::ADC: adc(v); break;
        case Op::SBC: sbc(v); break;
        case Op::LDA: r.a = v; nz(v); break;
        case Op::LDX: r.x = v; nz(v); break;
        case Op::LDY: r.y = v; nz(v); break;
        case Op::CMP: compare(r.a, v); break;
        case Op::CPX: compare(r.x, v); break;
        case Op::CPY: compare(r.y, v); break;
        case Op::BIT: flag(Z, (r.a & v) == 0); r.p = (r.p & ~(N | V)) | (v & (N | V)); break;
        default: throw std::logic_error("invalid read operation");
        }
    }
    void implied(Op op) {
        switch (op) {
        case Op::CLC: flag(C, false); break; case Op::SEC: flag(C, true); break;
        case Op::CLI: flag(I, false); break; case Op::SEI: flag(I, true); break;
        case Op::CLD: flag(D, false); break; case Op::SED: flag(D, true); break;
        case Op::CLV: flag(V, false); break;
        case Op::DEY: --r.y; nz(r.y); break; case Op::INY: ++r.y; nz(r.y); break;
        case Op::DEX: --r.x; nz(r.x); break; case Op::INX: ++r.x; nz(r.x); break;
        case Op::TXA: r.a = r.x; nz(r.a); break; case Op::TYA: r.a = r.y; nz(r.a); break;
        case Op::TAX: r.x = r.a; nz(r.x); break; case Op::TAY: r.y = r.a; nz(r.y); break;
        case Op::TSX: r.x = r.s; nz(r.x); break; case Op::TXS: r.s = r.x; break;
        case Op::NOP: break;
        default: throw std::logic_error("invalid implied operation");
        }
    }
    bool branch(Op op) const {
        switch (op) {
        case Op::BPL: return !(r.p & N); case Op::BMI: return r.p & N;
        case Op::BVC: return !(r.p & V); case Op::BVS: return r.p & V;
        case Op::BCC: return !(r.p & C); case Op::BCS: return r.p & C;
        case Op::BNE: return !(r.p & Z); case Op::BEQ: return r.p & Z;
        default: throw std::logic_error("invalid branch");
        }
    }

    Task execute() {
        if (reset_pending) {
            reset_pending = false;
            (void)co_await read(r.pc);
            (void)co_await read(r.pc);
            for (int i = 0; i < 3; ++i) {
                (void)co_await read(0x100 | r.s);
                --r.s;
            }
            r.p |= I;
            const auto low = co_await read(0xfffc);
            const auto high = co_await read(0xfffd);
            r.pc = static_cast<std::uint16_t>(low | (high << 8));
            co_return;
        }
        const auto interrupt = std::exchange(pending, Interrupt::none);
        const bool hardware = interrupt != Interrupt::none;
        const auto opcode_address = r.pc;
        const auto opcode = co_await (hardware ? read(r.pc) : fetch());
        const auto instruction = hardware ? Instruction{Op::BRK, Mode::imp} : instructions[opcode];
        const auto op = instruction.op;
        const auto mode = instruction.mode;
        if (op == Op::invalid) throw UnsupportedOpcode(opcode, opcode_address);
        if (op == Op::BRK) {
            (void)co_await (hardware ? read(r.pc) : fetch());
            (void)co_await write(0x100 | r.s, static_cast<std::uint8_t>(r.pc >> 8)); --r.s;
            (void)co_await write(0x100 | r.s, static_cast<std::uint8_t>(r.pc)); --r.s;
            // Vector selection precedes the status push: late NMI can redirect
            // IRQ/BRK without changing their already-selected stack contents.
            const bool use_nmi = interrupt == Interrupt::nmi || nmi_pending;
            if (use_nmi) nmi_pending = false;
            const std::uint16_t vector = use_nmi ? 0xfffa : 0xfffe;
            (void)co_await write(0x100 | r.s, hardware ? r.p : r.p | B); --r.s;
            r.p |= I;
            const auto low = co_await read(vector);
            const auto high = co_await read(vector + 1);
            r.pc = static_cast<std::uint16_t>(low | (high << 8));
            co_return;
        }
        if (mode == Mode::rel) {
            poll();
            const auto displacement = co_await fetch();
            if (branch(op)) {
                const int offset = displacement < 128 ? displacement : int(displacement) - 256;
                const auto target = static_cast<std::uint16_t>(r.pc + offset);
                (void)co_await read(r.pc);
                if ((r.pc & 0xff00) != (target & 0xff00)) {
                    poll();
                    (void)co_await read((r.pc & 0xff00) | (target & 0xff));
                }
                r.pc = target;
            }
            co_return;
        }
        if (op == Op::JSR) {
            const auto low = co_await fetch();
            (void)co_await read(0x100 | r.s);
            (void)co_await write(0x100 | r.s, static_cast<std::uint8_t>(r.pc >> 8)); --r.s;
            (void)co_await write(0x100 | r.s, static_cast<std::uint8_t>(r.pc)); --r.s;
            poll();
            const auto high = co_await read(r.pc);
            r.pc = static_cast<std::uint16_t>(low | (high << 8));
            co_return;
        }
        if (op == Op::JMP) {
            const auto low = co_await fetch();
            if (mode == Mode::abs) poll();
            const auto high = co_await fetch();
            const auto pointer = static_cast<std::uint16_t>(low | (high << 8));
            if (mode == Mode::ind) {
                const auto target_low = co_await read(pointer);
                poll();
                const auto target_high = co_await read((pointer & 0xff00) | ((pointer + 1) & 0xff));
                r.pc = static_cast<std::uint16_t>(target_low | (target_high << 8));
            } else r.pc = pointer;
            co_return;
        }
        if (op == Op::RTS || op == Op::RTI) {
            (void)co_await read(r.pc);
            (void)co_await read(0x100 | r.s); ++r.s;
            if (op == Op::RTI) {
                r.p = ((co_await read(0x100 | r.s)) | U) & ~B; ++r.s;
            }
            const auto low = co_await read(0x100 | r.s); ++r.s;
            if (op == Op::RTI) poll();
            const auto high = co_await read(0x100 | r.s);
            r.pc = static_cast<std::uint16_t>(low | (high << 8));
            if (op == Op::RTS) {
                poll();
                (void)co_await read(r.pc); ++r.pc;
            }
            co_return;
        }
        if (op == Op::PHA || op == Op::PHP || op == Op::PLA || op == Op::PLP) {
            (void)co_await read(r.pc);
            if (op == Op::PHA || op == Op::PHP) {
                poll();
                (void)co_await write(0x100 | r.s, op == Op::PHA ? r.a : r.p | B); --r.s;
            } else {
                (void)co_await read(0x100 | r.s); ++r.s;
                poll();
                const auto pulled = co_await read(0x100 | r.s);
                if (op == Op::PLP) r.p = (pulled | U) & ~B;
                else { r.a = pulled; nz(r.a); }
            }
            co_return;
        }
        if (mode == Mode::imp || mode == Mode::acc) {
            poll();
            (void)co_await read(r.pc);
            if (mode == Mode::acc) r.a = modify(op, r.a);
            else implied(op);
            co_return;
        }

        if (mode == Mode::imm) {
            poll();
            const auto operand = co_await fetch();
            apply(op, operand);
            co_return;
        }
        std::uint16_t effective{};
        switch (mode) {
        case Mode::zp: effective = co_await fetch(); break;
        case Mode::zpx: case Mode::zpy: {
            const auto base = co_await fetch();
            (void)co_await read(base);
            effective = static_cast<std::uint8_t>(base + (mode == Mode::zpx ? r.x : r.y));
            break;
        }
        case Mode::abs: case Mode::abx: case Mode::aby: {
            const auto low = co_await fetch();
            const auto high = co_await fetch();
            const auto base = static_cast<std::uint16_t>(low | (high << 8));
            effective = static_cast<std::uint16_t>(base + (mode == Mode::abx ? r.x : mode == Mode::aby ? r.y : 0));
            if (mode != Mode::abs && (stores(op) || modifies(op) || ((base ^ effective) & 0xff00)))
                (void)co_await read((base & 0xff00) | (effective & 0xff));
            break;
        }
        case Mode::izx: {
            const auto base = co_await fetch();
            (void)co_await read(base);
            const auto pointer = static_cast<std::uint8_t>(base + r.x);
            const auto low = co_await read(pointer);
            const auto high = co_await read(static_cast<std::uint8_t>(pointer + 1));
            effective = static_cast<std::uint16_t>(low | (high << 8));
            break;
        }
        case Mode::izy: {
            const auto pointer = co_await fetch();
            const auto low = co_await read(pointer);
            const auto high = co_await read(static_cast<std::uint8_t>(pointer + 1));
            const auto base = static_cast<std::uint16_t>(low | (high << 8));
            effective = static_cast<std::uint16_t>(base + r.y);
            if (stores(op) || ((base ^ effective) & 0xff00))
                (void)co_await read((base & 0xff00) | (effective & 0xff));
            break;
        }
        default: throw std::logic_error("invalid addressing mode");
        }
        if (stores(op)) {
            poll();
            (void)co_await write(effective, op == Op::STA ? r.a : op == Op::STX ? r.x : r.y);
        } else if (modifies(op)) {
            const auto old = co_await read(effective);
            (void)co_await write(effective, old);
            const auto changed = modify(op, old);
            poll();
            (void)co_await write(effective, changed);
        } else {
            poll();
            const auto operand = co_await read(effective);
            apply(op, operand);
        }
    }

    void clock() {
        if (faulted) throw std::logic_error("CPU requires reset after failure");
        try {
            if (cycle == std::numeric_limits<std::uint64_t>::max())
                throw std::overflow_error("CPU cycle counter exhausted");
            if (!task.handle || task.handle.done()) {
                task = execute();
                task.handle.resume();
            }
            // A suspended frame describes precisely the access for this cycle.
            if (!bus.acquire(address, writing, cycle + 1)) {
                if (writing) throw std::logic_error("6502 writes cannot be halted");
                ++cycle;
                if (nmi && !last_nmi) nmi_pending = true;
                last_nmi = nmi;
                return;
            }
            if (writing) bus.write(address, value, cycle + 1);
            else value = bus.read(address, cycle + 1);
            ++cycle;
            if (nmi && !last_nmi) nmi_pending = true;
            last_nmi = nmi;
            task.handle.resume();
            if (task.handle.promise().error) std::rethrow_exception(task.handle.promise().error);
        } catch (...) {
            faulted = true;
            throw;
        }
    }
};

Mos6502::Mos6502(Bus& bus, Variant model) : impl_(std::make_unique<Impl>(bus, CpuState{}, true, model)) {}
Mos6502::Mos6502(Bus& bus, CpuState initial, Variant model) : impl_(std::make_unique<Impl>(bus, initial, false, model)) {}
Mos6502::~Mos6502() = default;
void Mos6502::clock() { impl_->clock(); }
void Mos6502::reset() {
    impl_->task = Impl::Task{};
    impl_->faulted = false;
    impl_->reset_pending = true;
    impl_->pending = Impl::Interrupt::none;
    impl_->nmi_pending = false;
    impl_->last_nmi = impl_->nmi;
}
void Mos6502::set_irq(bool asserted) noexcept { impl_->irq = asserted; }
void Mos6502::set_nmi(bool asserted) noexcept { impl_->nmi = asserted; }
CpuState Mos6502::state() const noexcept { return impl_->r; }
std::uint64_t Mos6502::cycles() const noexcept { return impl_->cycle; }
bool Mos6502::at_instruction_boundary() const noexcept {
    return !impl_->faulted && !impl_->reset_pending && (!impl_->task.handle || impl_->task.handle.done());
}
} // namespace rse::cpu
