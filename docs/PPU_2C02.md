# 2C02 PPU (M4)

## Chip and memory boundary

`rse::video::Rp2c02` is an independent NTSC chip model. It owns register latches,
OAM, secondary OAM, palette RAM, rendering state, and double-buffered indexed
frames. Cartridge CHR and nametable storage/mirroring are supplied through
`PpuBus`. The NES adapter delegates to the M2 memory map; the chip does not know
cartridge formats, CPU instructions, SDL3, Vulkan, or host time.
Frame buffers are output captures, not emulated addressable chip memory.

`clock()` processes the position returned by `state()` and advances one PPU dot.
Register accesses occur between clocks, at the current completed-dot timestamp.
The native register selector decodes its low three bits; NES CPU address decoding
and CPU/PPU phase conversion remain M5 work. External fetches report the completed
read on the second dot of each two-dot transaction, with a 1-based dot timestamp.
CPU-port transactions use the completed-dot timestamp and do not advance time.

Reset begins at pre-render line 261, dot 0, even parity. It initializes OAM and
secondary OAM to `$FF`, palette/registers/frame buffers to zero, and accepts
register writes immediately. This deterministic initialization is not physical
power-on emulation: warm-up write inhibition, DRAM/open-bus decay, analog output,
EXT pins, and CPU-alignment-dependent half-dot collisions are outside this contract.
PPUADDR commits at the write boundary. PPUMASK layer enables pass through a
three-dot pipeline; palette/grayscale/clip controls use the register value.
The physical 3-4-dot alignment-dependent enable delay and other sub-dot write
delays/corruption require later system-phase validation.
Unusual rendering-toggle/OAMADDR interactions are not exhaustively modeled.
The named OAM row-copy case and dot-level overflow behavior below do not imply
complete G/H silicon-revision conformance.

`reset()` starts a new chip timeline without clearing external memory. Bus errors
fault accesses/clocks until reset; partial work is not rolled back. Inspection
and completed-frame access do not touch the memory bus. The bus must outlive the
chip. Operations are single-threaded and callbacks must not reenter the chip.

## Observable behavior and reference plan

The references for M4 are the NESdev [register](https://www.nesdev.org/wiki/PPU_registers),
[scrolling](https://www.nesdev.org/wiki/PPU_scrolling),
[rendering](https://www.nesdev.org/wiki/PPU_rendering),
[sprite evaluation](https://www.nesdev.org/wiki/PPU_sprite_evaluation), and
[memory map](https://www.nesdev.org/wiki/PPU_memory_map) specifications.
Project-owned fixtures exercise these named boundaries without ROM assets:

- Register cases: shared scroll/address write toggle; status/open-bus reads;
  buffered data reads; palette aliases, grayscale, and shadow-buffer refill;
  OAM indexing/write protection; normal and rendering-time address increments.
- Timing cases: 341 dots/262 lines, odd rendered pre-line skips, vblank/NMI and
  status-read suppression, pre-render flag clearing, and selected fetch addresses.
- Rendering cases: analytical tile/attribute scenes, fine scrolling and nametable
  wrap, rendering-time palette/control/scroll writes, sprite priority/clipping,
  sprite-zero overlap, flips, 8x16 addressing, and diagonal overflow scanning.
- Headless frames: compare every sample to an independent spatial oracle for
  static scenes, then compare a recorded FNV-1a hash using fixed palette contents.
  Golden results must be derived from that oracle, not captured from this PPU.

Visible samples are emitted at dots 1-256 of lines 0-239. Status hit observations
use those pixel boundaries. Sprite evaluation scans primary OAM on odd dots and
updates secondary OAM/overflow on even dots, selecting the next scanline's units.
There is no sprite evaluation for line 0 on the pre-render line. Background
fetching, scroll reloads, and sprite fetches continue if either layer is enabled.
Unimplemented alignment/analog effects above are not evidence of hardware parity.

## Output contract

Each 256x240 sample is a 9-bit code: six palette-color bits and three emphasis
bits. Grayscale is applied to the color bits. The fixed test palette maps these
codes deterministically; display RGB/analog palette conversion remains separate.
Frames publish at line 241, dot 1, including when a status read suppresses vblank.
`frame()` remains valid until the next publication or reset; clients must copy
samples they retain. No GPU objects or frontend resources are stored in the chip.

## Golden frame records

FNV-1a-32 starts at `2166136261`, consumes each 9-bit sample low byte then high
byte in scan order, and multiplies by `16777619` modulo 2^32 per byte. Palette
RAM is fixed to `(canonical_index * 3 + 5) & 63`, including palette aliases.
The test-only spatial oracle resolves coordinates and sprite priority directly;
it does not use the chip's fetch/shift-register code.

| Scene | Expected hash |
| --- | --- |
| Background | `23ac8dc5` |
| Scroll/nametable wrap | `7a4b8f51` |
| Grayscale and emphasis | `7b6b09f5` |
| 8x8 sprites | `f4f5ed3a` |
| 8x16 sprites | `139daa35` |
| Background table change after line 100 | `3caafe45` |
| Fine-X change at line 80, pixel 128 | `9d9b89c5` |
| Mask color change at line 120, pixel 128 | `344d9805` |

`rse-ppu-render-tests --print-goldens` regenerates these records using only the
oracle. Normal tests compare both the oracle and actual output to these pinned
values, verify every pixel, and repeat after reset. They also retain a published
frame while the other buffer draws. Expected frame lengths are 89,342 dots and
89,341 on odd rendered frames; blank frames do not skip a dot.

M4 validation for implementation commit `e69d2db`: Windows MSVC 2022 and Linux
GCC configure, Debug/Release builds, and all eight CTest suites pass. The eight
golden scenes pass per-pixel checks and reset replay, alongside the named
register, frame/fetch timing, sprite, and NROM memory-wiring cases. Linux address,
leak, and undefined-behavior sanitizers pass in
[CI run 43569186047](https://github.com/particlesector/retro-silicon-emulator/actions/runs/43569186047).
These results cover the declared dot-level contract, not the excluded silicon
effects or a complete CPU/PPU-integrated NES.
