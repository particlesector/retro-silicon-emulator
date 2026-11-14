# First emulator MVP

## Scope

The first MVP loads supported NES cartridges and provides verified execution,
video, audio, and controller input through the desktop frontend on Windows and
Linux. Target NTSC NES hardware and mapper 0 (NROM), with PRG ROM and CHR ROM or
CHR RAM. Support one standard controller and horizontal or vertical mirroring.

Defer PAL, other mappers, NES 2.0, battery saves, save states, rewind, debugger
UI, additional peripherals, GPU compute acceleration, and other systems.
FPGA equivalence and signal-level simulation are separate future goals.

## Completion rules

- Complete each milestone's checklist before marking it done. Use small PRs.
- Before implementation, name the milestone's reference tests, expected results,
  and observation boundaries. Record sources and licensing for external assets;
  do not bundle copyrighted game ROMs or proprietary firmware.
- Behavioral checks must run in Debug and Release. CMake configure, build, and
  CTest must pass for affected targets; the core must remain independent of SDL3
  and Vulkan.
- New requirements outside these checklists go into follow-up work. Change MVP
  scope explicitly before treating them as acceptance blockers.
- Visual plausibility alone does not establish timing accuracy. Record exact
  test results and limitations before making accuracy claims.

## 1. Core timing and lifecycle

Contract and evidence: [Core timing and lifecycle](CORE_TIMING.md).

- [x] Document tick units, equal-time ordering, clock-domain conversion, and
  whether the target timestamp is included when advancing.
- [x] Define registration, ownership, reset, invalid input, and component failure
  behavior, including the state callers may observe after a failure.
- [x] Test multiple components, deterministic ordering, equal timestamps, rewind
  rejection, null registration, registration after advancement, and reset.
- [x] Repeated runs and different advancement chunk sizes produce identical
  results for the same initial state and scheduled inputs.

## 2. Memory bus and cartridge loading

- [ ] Parse supported iNES mapper 0 images with bounded reads and explicit size
  validation; document handling of trainers, flags, and unsupported variants.
- [ ] Reject malformed, truncated, and unsupported cartridges with clear errors.
- [ ] Test CPU RAM mirroring, NROM PRG mapping, CHR ROM/RAM access, nametable
  mirroring, and the chosen unmapped-read and ROM-write behavior.
- [ ] Verify memory maps using synthetic fixtures without frontend dependencies.

## 3. 2A03 CPU

- [ ] Implement all documented instructions, addressing modes, flags, stack
  operations, reset, IRQ, and NMI, including 2A03 decimal-mode behavior.
- [ ] Verify instruction results and timestamped bus accesses, including branch
  and page-crossing timing and interrupt entry, against named reference cases.
- [ ] Define unsupported opcode behavior explicitly; add undocumented opcodes
  only when required by the agreed MVP acceptance programs.
- [ ] Automated tests compare CPU state and cycle counts with pinned expected
  results and report the first mismatch.

## 4. PPU and video

- [ ] Implement CPU-visible registers, VRAM/OAM access, scrolling, background and
  sprite output, palette selection, and supported mirroring behavior.
- [ ] Test sprite priority, sprite-zero hit, sprite overflow behavior, vblank,
  NMI generation, and register side effects at defined timing boundaries.
- [ ] Verify frame timing and selected register/bus events against reference
  cases, including rendering-time register writes.
- [ ] Produce deterministic frames in headless tests and compare selected frame
  hashes against recorded expected output with a fixed palette.

## 5. System integration and input

- [ ] Connect CPU, PPU, cartridge, and controller through the documented NTSC
  clock relationship and memory map.
- [ ] Verify OAM DMA transfers, CPU stalls, and NMI/IRQ delivery and ordering.
- [ ] Test controller strobe and serial reads with recorded button sequences.
- [ ] Run selected NROM programs headlessly for a fixed simulated duration;
  repeated runs produce identical state, event, and frame checksums.

## 6. APU and audio

- [ ] Implement pulse, triangle, noise, and DMC channels, register behavior,
  frame sequencing, and audio mixing.
- [ ] Test channel timing, frame/DMC interrupts, sample fetching, CPU stalls,
  and interactions with OAM DMA using named reference cases.
- [ ] Generate deterministic audio samples for fixed initial state and inputs
  at a documented sample rate; compare with recorded expected results.
- [ ] Desktop audio plays continuously in the agreed smoke-test session without
  sustained underruns; host audio timing does not change simulated execution.

## 7. Desktop MVP

- [ ] Select a cartridge, start execution, reset, and exit normally; invalid or
  unsupported cartridges produce an actionable error.
- [ ] Present the emulated frame through Vulkan with a preserved aspect ratio;
  window resizing and closing work on Windows and Linux.
- [ ] Map a documented keyboard layout and one standard gamepad to NES input;
  losing window focus clears held keyboard buttons.
- [ ] Provide real-time pacing and synchronized video/audio without changing
  deterministic core results.
- [ ] Build Release packages with required runtime dependencies and verify
  launch, video, audio, input, reset, and exit on both supported platforms.

## 8. MVP acceptance

- [ ] Freeze a small acceptance set of legally available NROM programs before
  desktop acceptance testing. Record versions/hashes, license/source, inputs,
  duration, and expected results. Cover video, audio, input, and reset.
- [ ] All earlier milestone checklists and the named automated reference suite
  pass; test failures cannot be waived by a successful visual demo.
- [ ] Record successful Release smoke tests on Windows and Linux, including a
  continuous 10-minute session on each platform with no crash or sustained
  audio underruns. Record host hardware and pacing performance.
- [ ] Publish reproducible build/run/test instructions, supported cartridge
  constraints, test results, and remaining limitations.
- [ ] Keep deferred features outside the MVP release; make no NES accuracy,
  Vulkan rendering, or FPGA equivalence claims beyond recorded verification.
