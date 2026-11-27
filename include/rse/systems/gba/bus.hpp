// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "display.hpp"
#include "timers.hpp"
#include <rse/audio/gba_apu.hpp>
#include <rse/cpu/arm7tdmi.hpp>
#include <array>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace rse::gba {

inline constexpr std::size_t bios_size = 0x4000;
inline constexpr std::size_t cartridge_header_size = 0xc0;
inline constexpr std::size_t cartridge_max_size = 0x2000000;
inline constexpr std::uint32_t clock_hz = 16777216;

struct EntryFetch {
    std::uint64_t tick; // Beginning of the first logical instruction read at entry.
    std::uint32_t opcode;
    unsigned width; // Bytes.
    bool operator==(const EntryFetch&) const = default;
};

enum class TimingKind { vblank_enter, frame, vblank_exit, timer_overflow, dma_start, dma_complete, irq_acknowledge,
                        sound_fifo_request };
struct TimingEvent {
    std::uint64_t tick;
    TimingKind kind;
    unsigned value{}; // Timer or DMA channel, or the bits cleared by IF acknowledgement.
    bool operator==(const TimingEvent&) const = default;
};

// Owns GBA media and address decoding and clocks the board peripherals.
// CPU access methods use aligned addresses for halfwords and words. Each
// access advances elapsed master ticks, including the selected memory waits.
class Bus final : public cpu::Arm7Bus {
public:
    static constexpr std::size_t audio_limit = 65536; // Individual interleaved samples.
    Bus(std::span<const std::uint8_t> bios, std::span<const std::uint8_t> cartridge, bool capture_audio = false);
    void reset() noexcept;
    [[nodiscard]] std::uint64_t ticks() const noexcept { return ticks_; }
    [[nodiscard]] std::optional<EntryFetch> entry_fetch() const noexcept { return entry_fetch_; }
    [[nodiscard]] bool irq() const noexcept { return ime_ && wake(); }
    [[nodiscard]] bool halted() const noexcept { return halted_ && !wake(); }
    [[nodiscard]] bool dma_pending() const noexcept;
    // Run one DMA read/write pair, pausing at the limit when needed.
    void step_dma(std::uint64_t limit = Timers::never);
    void idle_to_lcd_event();
    void idle(std::uint64_t limit);
    void schedule_keys(std::uint64_t tick, std::uint16_t pressed);
    const Display& display() const noexcept { return display_; }
    [[nodiscard]] std::vector<std::int16_t> take_audio();
    // Optional synchronous timing observer. It must neither reenter the bus
    // nor modify the emulated machine. Reset preserves the observer.
    using TimingSink = void (*)(void*, TimingEvent) noexcept;
    void set_timing_sink(TimingSink sink, void* context) noexcept { sink_ = sink; sink_context_ = context; }
    // Machine-owned CPU continuation across exact host tick boundaries.
    void begin_cpu_attempt(std::uint64_t limit) noexcept;
    void complete_cpu_attempt() noexcept;
    void advance_irq_entry() { advance(1); }
    void branch() noexcept override { previous_ = {}; }

    std::uint32_t fetch_arm(std::uint32_t address) override;
    std::uint16_t fetch_thumb(std::uint32_t address) override;
    std::uint32_t load_word(std::uint32_t address) override;
    std::uint16_t load_half(std::uint32_t address) override;
    std::uint8_t load_byte(std::uint32_t address) override;
    void store_word(std::uint32_t address, std::uint32_t value) override;
    void store_half(std::uint32_t address, std::uint16_t value) override;
    void store_byte(std::uint32_t address, std::uint8_t value) override;

private:
    enum class Access { none, fetch, load, store };
    struct Sequence {
        std::uint32_t address{};
        unsigned width{};
        Access kind{};
        bool follows(std::uint32_t next, Access access) const noexcept;
    };
    struct Dma {
        std::uint32_t source{}, destination{};
        std::uint32_t current_source{}, current_destination{};
        unsigned count{}, remaining{};
        std::uint16_t control{};
        bool pending{};
        Sequence reads{}, writes{};
    };
    enum class DmaPhase { startup, read, write };
    struct DmaTransfer {
        unsigned channel{}, clocks{};
        std::uint32_t value{};
        DmaPhase phase{};
    };

    [[nodiscard]] bool wake() const noexcept { return (ie_ & if_) != 0; }
    std::uint32_t fetch(std::uint32_t address, unsigned width);
    std::uint32_t read(std::uint32_t address, unsigned width, Access kind);
    void write(std::uint32_t address, unsigned width, std::uint32_t value);
    std::uint32_t read_data(std::uint32_t address, unsigned width, bool bios_access) const;
    void write_data(std::uint32_t address, unsigned width, std::uint32_t value);
    std::uint16_t read_io(unsigned address) const;
    void write_io(unsigned address, std::uint16_t value, std::uint16_t mask);
    void write_dma(unsigned address, std::uint16_t value, std::uint16_t mask);
    void trigger_dma(unsigned timing);
    void trigger_sound_dma(unsigned requests);
    static bool sound_dma(unsigned channel, const Dma& dma) noexcept;
    void finish_dma_unit(Dma& dma, unsigned channel, unsigned width, std::uint32_t value);
    void update_key_irq() noexcept;
    void emit(TimingKind kind, unsigned value = 0) const noexcept;
    unsigned access_ticks(std::uint32_t address, unsigned width, bool sequential) const noexcept;
    unsigned ticks_to_lcd_event() const noexcept;
    std::uint64_t ticks_to_event() const noexcept;
    void handle_lcd_event();
    void advance(unsigned ticks);
    bool consume(unsigned& clocks, std::uint64_t limit);
    void advance_cpu_access(unsigned clocks);

    std::array<std::uint8_t, bios_size> bios_{};
    std::vector<std::uint8_t> rom_;
    std::array<std::uint8_t, 0x40000> ewram_{};
    std::array<std::uint8_t, 0x8000> iwram_{};
    std::array<std::uint8_t, 0x400> palette_{}, oam_{};
    std::array<std::uint8_t, 0x18000> vram_{};
    std::array<std::uint16_t, 0x200> io_{};
    std::array<Dma, 4> dma_{};
    std::optional<unsigned> dma_owner_;
    std::optional<DmaTransfer> dma_transfer_;
    Display display_;
    Timers timers_;
    audio::GbaApu apu_;
    std::vector<std::int16_t> pcm_;
    bool capture_audio_{};
    TimingSink sink_{};
    void* sink_context_{};
    std::multimap<std::uint64_t, std::uint16_t> input_;
    std::uint16_t pressed_{}, keycnt_{};
    bool key_match_{};
    Sequence previous_{};
    // ARM7TDMI can issue one fetch and at most sixteen data accesses per instruction.
    std::array<std::uint32_t, 17> cpu_results_{};
    unsigned cpu_count_{}, cpu_cursor_{}, cpu_clocks_{};
    std::uint64_t cpu_limit_{Timers::never};
    bool cpu_bounded_{};
    std::optional<EntryFetch> entry_fetch_;
    std::uint64_t ticks_{};
    std::uint32_t instruction_address_{}, bios_latch_{}, opcode_latch_{};
    unsigned line_{}, dot_{};
    std::uint16_t ie_{}, if_{}, waitcnt_{};
    bool ime_{}, halted_{};
};

}
