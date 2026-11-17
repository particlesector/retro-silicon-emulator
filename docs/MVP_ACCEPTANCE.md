# NROM MVP acceptance (M8)

## Accepted baseline

M8 is complete for the documented NTSC NROM subset. At acceptance:

- All 18 Windows desktop Debug/Release suites passed; the frontend-free suite
  had 17 tests. Frozen ROM bytes and deterministic observations are pinned below.
- The extracted Windows Release package ran for 600,001 ms with zero audio
  starvation and five Vulkan readbacks. It produced 36,050 frames and 599,857
  simulated ms, including the scripted pauses/resets. Source buffering targeted
  12 ms on the recorded WASAPI device. Software response observations were
  approximately 28-32 ms, not physical button-to-screen measurements.
- Linux Debug/Release, sanitizers and extracted-package virtual-display checks
  passed in [CI run 43691747294](https://github.com/particlesector/retro-silicon-emulator/actions/runs/43691747294).
  Virtual display/dummy audio do not establish physical playback quality.
- On November 15, 2026, project owner Chris manually approved a ten-minute smoke
  session on native Ubuntu 24.04 LTS: the blue screen and constant audio ran
  without issues. He also reported successful extended SMB1 gameplay. Hardware,
  exact build ID, logs and pacing counters were unavailable; this was explicitly
  accepted as owner-reported manual validation.

The Windows host was Windows 11 Pro build 26200, AMD Ryzen AI Max+ 395 / Radeon
8060S (driver 32.0.31032.1003), SDL 3.4.14 and MSVC 19.44. The tested executable's
SHA-256 was `86c3b882af2517a5a244f2b91b9b44807f4dde7932f78ee37e3ea1270ee05b07`.
Colors, tear-free presentation and gameplay feel were approved by the owner.

Detailed counters, failed WSLg attempts, earlier builds and their limitations are
preserved in [validation history](MVP_VALIDATION_HISTORY.md). The scope below
remains the limit of the acceptance claim; it is not a broad compatibility survey.

## Frozen set

Acceptance set v1 is pinned in [manifest.json](../tests/acceptance/manifest.json).
Its programs are project-owned Apache-2.0 source-generated NROM fixtures, carried
forward byte-for-byte from M7. No copyrighted game ROMs or firmware are included.
The source generators are [acceptance_programs.hpp](../tests/acceptance_programs.hpp)
and [nes_programs.hpp](../tests/nes_programs.hpp). Changing ROM bytes requires a
new fixture version; do not silently replace the frozen SHA-256 values.

| Program | Purpose | SHA-256 |
| --- | --- | --- |
| `rse-loop-v1.nes` | CPU/scheduler, blank frame, silent PCM | `fa40ee2ef29c43fd342914d25e0e12db4f45596a731894109d1f03f9972e0d1c` |
| `rse-audio-dma-v1.nes` | Five sound channels, looping DMC, OAM DMA | `d7d3446a2b611a9372e9953f926ca1802b633261b0e54fcf2c65cd56147fe41d` |
| `rse-av-input-v1.nes` | Video/audio, NMI controller reads, palette response | `5dca096c5c119c8df7fd7c93b5c361e9d84bf01f88d512bfc310097e08bbc5d8` |

The two additional companion files in the manifest test malformed-header and
unsupported-mapper recovery. They are rejected, never executed.

Each headless program runs for 12 published frames with A held before frames
5-8 and all buttons released otherwise. Input changes occur at current master
tick + 1. The combined program must finish with 11 completed NMI handlers,
released buttons in RAM $10, and a solid $21 palette-code frame. All programs
produce 9,523 PCM samples. Exact elapsed ticks and regression checksums are in
the manifest. These captures are not independent hardware accuracy oracles;
the earlier chip reference cases remain required.

`acceptance-replay-tests` checks the pinned observations, 997-tick chunk replay,
and deterministic power-on reset. `acceptance-fixture-hashes` regenerates all
five files, uses CMake's SHA-256 to check size/hash, and compares printed
observations against the manifest. Observations use FNV-1a, encoding each numeric
field as eight little-endian bytes, including unsigned 16-bit PCM bit patterns.
State fields and their order are defined in `tests/acceptance_tests.cpp`; this
is a diagnostic projection, not a complete save state or public trace ABI.

## Build, test, and generate

Use the [README](../README.md) for Windows/Linux configure/build/test commands
and [desktop guide](DESKTOP.md#release-package) for Release packaging. A frontend-
free build still runs 17 CTest suites; a desktop build adds the producer lifecycle
and audio-continuity suite (18 total). Required earlier reference groups are
core lifecycle, cartridge/memory, both CPU vector suites and CPU behavior,
PPU registers/rendering/memory, devices/arbitration, NES system, PCM, desktop
policy/replay, and CLI. A visual demo never substitutes for a failing suite.

CTest leaves the verified set under `<build>/acceptance-set`, including source,
license, manifest, and `observations.txt`. To generate/verify it explicitly:

```powershell
cmake -DGENERATOR=C:/progs/retro-silicon-emulator/build/m8/Release/rse-acceptance-tests.exe -DOUTPUT=C:/progs/retro-silicon-emulator/build/m8/acceptance-set -P cmake/VerifyAcceptance.cmake
```

On Linux use the corresponding absolute executable/build paths. No Python,
assembler, downloaded ROM, or additional hashing library is required.

## Ten-minute desktop session

Freeze and verify the set before starting desktop acceptance. Run an extracted
Release package with the combined fixture, keeping its two companions adjacent:

```sh
rse-desktop --smoke /absolute/path/acceptance-set/rse-av-input-v1.nes --seconds 600
```

The script exercises the [M7 desktop flow](DESKTOP.md#reproduce-the-smoke-test)
before steady playback. It must run for the full requested duration; early close
fails. The result must show two successful loads, one reset, three expected load
errors, at least three successful Vulkan readbacks, and zero unsatisfied SDL audio
requests. No crash or sustained underrun is permitted. Intentional reset and
minimize/restore pauses are part of the script, not underruns.

Keep the log and record OS/build, CPU/GPU and driver, SDL version, renderer/audio
backend, duration, generated frames, queue peaks, and timing counters. `simulated_ms`
accumulates master ticks across resets; compare it to `elapsed_ms` for throughput.
It includes prebuffer/lookahead work and excludes discarded buffers only from
playback, so it is not exact audible time. `max_frame_work_us` measures the largest
frame-generation/queue work interval, not a GPU or speaker-latency benchmark.
The producer-thread candidate instead reports `max_chunk_work_us` for its roughly
1 ms advancement blocks and `target_audio_bytes` for negotiated source buffering.
No emulated cycles are skipped to satisfy host pacing.
Also verify audible continuity and compare simulated progress with wall time.
A stalled host sink can stop consuming queued PCM without an unsatisfied SDL
request; a zero starvation counter alone is insufficient to pass acceptance.

Run on Windows and Linux with an actual display/audio path for the physical-host
gate. Xvfb/lavapipe/dummy-audio CI verifies the integration but cannot establish
physical Linux audio, GPU-driver compatibility, or subjective sound quality.
Record WSLg/device bridges explicitly if that is the chosen Linux environment.

## Supported boundary and deferred work

This acceptance set establishes execution of these generated programs only.
It is not a commercial-game compatibility survey or a claim of full NES accuracy.
Supported cartridges remain NTSC clean iNES mapper 0, 16/32 KiB PRG, 8 KiB CHR ROM
or CHR RAM, horizontal/vertical mirroring, and one standard controller. See
[cartridge restrictions](NES_MEMORY.md) and the CPU/PPU/APU contracts for exact
limits. Unsupported opcodes fail explicitly. PPU startup/alignment quirks, APU
subcycle collisions/DMC stop bugs, and analog color/audio equivalence remain
outside verified behavior. The RGB palette is approximate.

PAL, other mappers, NES 2.0, battery saves, save states/rewind, debugger UI, other
systems, GPU compute emulation, and FPGA equivalence remain deferred. This work
adds acceptance evidence rather than expanding that scope.

## Subsequent MVP

The proposed [AI MVP acceptance](AI_MVP_ACCEPTANCE.md) defines separate M20
release gates. This historical M8 record and frozen fixtures remain unchanged
in meaning; deferred features above describe the M8 baseline, not the new plan.
