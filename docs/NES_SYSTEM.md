# NTSC NES

## Supported cartridges

`Cartridge::parse` accepts a byte span and owns a copy of accepted data.
`Cartridge::load_file` reads at most the largest supported image plus one byte;
both entry points reject truncated and trailing data. Parse failures throw
`std::invalid_argument` with a reason; file I/O failures throw `std::runtime_error`.
Loading a new cartridge does not mutate an existing one.

Accept only clean iNES headers with mapper 0, 16 or 32 KiB PRG ROM, and 8 KiB
CHR ROM or zero CHR ROM banks (allocating 8 KiB CHR RAM). Only the mirroring
flag may be set in header byte 6. Reject trainers, battery-backed memory,
four-screen layouts, other mappers/consoles, NES 2.0, PAL, and nonzero header
bytes 8-15. No repair of dirty legacy headers or guessing hardware variants.
Byte 8 zero is treated as unspecified: this supported NROM subset has no PRG RAM.
Explicit PRG RAM declarations and unofficial header extensions are unsupported.

## Chips and timing

The machine clocks the NTSC 2C02 every four master ticks and the Ricoh CPU every twelve. At coincident ticks it applies scheduled inputs, clocks the PPU, routes NMI and publishes frame observations before the CPU/DMA slot. Reset recreates a deterministic power-on timeline. See the [NES machine](../include/rse/systems/nes/machine.hpp) and [session metadata](SESSIONS.md#nes-built-in).

| Component | Implemented behavior and limits |
| --- | --- |
| 6502 | All 151 documented opcodes. The reusable NMOS variant supports decimal arithmetic and the Ricoh 2A03 executes binary arithmetic. A denied read leaves the transaction pending while pins and CPU clocks advance. Unsupported opcodes fault after fetch. [CPU interface](../include/rse/cpu/mos6502.hpp) |
| 2C02 | The chip owns OAM, palette, dot-level rendering and published indexed frames. The machine supplies CHR and mirrored nametable RAM. Power-on state and display color conversion are deterministic approximations. [PPU interface](../include/rse/video/rp2c02.hpp) |
| APU and DMA | Pulse, triangle, noise and DMC feed deterministic 48 kHz mono PCM. The system arbitrates DMC and OAM DMA on the CPU bus and routes APU IRQ. Capture and drain PCM regularly when enabled. [APU interface](../include/rse/audio/rp2a03_apu.hpp) |
| Input | One standard controller latches eight buttons through its shift register. Input events carry future master ticks. Equal-time events preserve insertion order. |

## CPU memory and observation

2 KiB of CPU RAM mirrors across $0000–$1FFF. NROM PRG maps at $8000–$FFFF. $2000-$3FFF mirrors native PPU registers;
$4014 requests OAM DMA and $4016 strobes/reads controller 1. Unconnected controller
port 2 drives zero on its low lines. Unmapped reads retain the CPU bus latch;
controller reads use NES-001 wiring: bits 0-4 driven, latched high bits retained.
NES-101/Famicom port differences are not implied. $4000-$4013, $4015 and writes
to $4017 connect the APU; $4015 preserves CPU-latch bit 5. The external PPU bus still owns cartridge/nametable memory;
palette and OAM remain inside the PPU.

Inspection of CPU RAM/PRG and chip diagnostic state is non-mutating. It must not
read device ports. Optional bounded traces record master ticks, source-domain
clock numbers, access kind, address, and data in deterministic order. These are
diagnostic records, not a frozen trace-file ABI or serialized machine state.
On execution/trace errors, the machine faults; partial work is not rolled back.
Reset reconstructs a deterministic power-on timeline for the same cartridge and
profile, clears queues/traces/RAM/chip state, and releases controller buttons.
It is not a hardware warm-reset pin model.

## Headless runner

After building, run a supported mapper-0 iNES image for a bounded duration:

```sh
rse-cli run example.nes --ticks 720089
rse-cli run example.nes --frames 2
rse-cli run example.nes --frames 2 --system nes-ntsc
```

On Windows, the executable is in `build/<configuration>/Release` for a
multi-configuration build; use the build directory chosen in the README.
Counts are unsigned decimal 64-bit integers; zero is valid. Frame runs stop at
actual frame publication. No video window or audio output is produced.

Success prints one JSON record with format `rse.run.v1`, elapsed master `ticks`,
`cpu_cycles` (including DMA holds), published `frames`, and decimal `pc`.
Exit codes: 0 success, 2 arguments, 3 unsupported/malformed cartridge, 4 execution
failure, 5 cartridge I/O failure, 6 other loading failure. Errors go to stderr.
Unsupported CPU opcodes fail explicitly.

See [desktop playback](DESKTOP.md) for audio and video output.

## Deterministic regression fixtures

Run CTest after the [platform build](../README.md#build-and-test). `acceptance-fixture-hashes` regenerates the three project-owned NROM fixtures and two rejection cases. `acceptance-replay-tests` checks state, events, frames and PCM across reset and different step sizes. Generate the fixtures with `rse-acceptance-tests --write-set <directory>`. Expected sizes and digests live in the [frozen manifest](../tests/acceptance/manifest.json). For chip-specific observations and the original hardware limits, see the [M20 documentation snapshot](https://github.com/particlesector/retro-silicon-emulator/tree/c592dc7857da82cfceceda64d1fc0e27ded713e0/docs).
