# NES memory and cartridge loading (M2)

## Scope and contract

NES-specific code lives in `rse_nes`, independent of the CLI and SDL3/Vulkan.
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

`Bus` owns its cartridge. CPU RAM and nametable RAM start at zero as a
deterministic initialization policy, not a claim about hardware power-on state.
CHR RAM starts at zero. No soft-reset memory policy is introduced in M2.

| Access | Mapping |
| --- | --- |
| CPU `$0000-$1FFF` | 2 KiB RAM, mirrored every `$0800` bytes |
| CPU `$8000-$FFFF` | PRG ROM; 16 KiB repeats twice, 32 KiB maps directly |
| CPU other addresses | Reads return zero; writes ignored (devices deferred) |
| PPU `$0000-$1FFF` | CHR ROM (writes ignored) or writable CHR RAM |
| PPU `$2000-$2FFF` | 2 KiB nametable RAM; horizontal `[A,A,B,B]`, vertical `[A,B,A,B]` |
| PPU `$3000-$3EFF` | Mirror of `$2000-$2EFF` |
| PPU `$3F00-$3FFF` | External bus reads zero/writes ignored; palette RAM is inside the PPU chip |

PPU addresses are masked to 14 bits. CPU writes never change PRG ROM. CPU
PPU-register access, APU, controllers, DMA, bus conflicts, and open-bus latches
are not implemented here. The [2C02 chip](PPU_2C02.md) implements its own palette
and native CPU register ports; the [M5 machine](NES_SYSTEM.md) supplies CPU device
decoding and an open-bus latch above this raw storage map.
The zero-read policy is a placeholder for unconnected
devices, not hardware open-bus behavior. Bus operations do not advance time.

## Reference and acceptance tests

Reference specifications: [iNES](https://www.nesdev.org/wiki/INES),
[NROM](https://www.nesdev.org/wiki/NROM),
[CPU memory map](https://www.nesdev.org/wiki/CPU_memory_map), and
[PPU memory map](https://www.nesdev.org/wiki/PPU_memory_map).
Implementation and fixtures are project-owned; no external ROM assets are used.

`tests/nes_memory_tests.cpp` observes byte reads/writes and loader errors:

- `loader`: valid sizes/flags, copied ownership, all truncated lengths, trailing
  bytes, malformed signatures, unsupported header fields, and file I/O errors.
- `cpu_map`: all CPU RAM mirrors, both PRG sizes and their boundary addresses,
  ignored ROM/unmapped writes, and zero reads from unconnected devices.
- `ppu_map`: CHR ROM protection, CHR RAM writes, both nametable layouts and
  `$3000` mirrors, address wrapping, and deferred palette behavior.

Tests compare generated patterns and fixed expected values in Debug and Release.
Passing these cases verifies this memory subset, not NES execution or timing.

M2 validation: Windows MSVC 2022 configure, build, and both CTest suites passed
in Debug and Release. Linux GCC Debug and Release configure, build, and both
CTest suites passed in
[CI run 43563914794](https://github.com/particlesector/retro-silicon-emulator/actions/runs/43563914794)
for implementation commit `4979647`.
