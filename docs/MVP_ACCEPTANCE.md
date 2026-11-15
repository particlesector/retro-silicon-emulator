# NROM MVP acceptance (M8)

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

## Results

The set and expected observations were committed as `5005e45` before M8 desktop
testing. All 17 suites pass in Windows/MSVC Debug and Release, local Ubuntu/GCC
Release, and Linux CI Debug/Release plus ASan/UBSan with leak detection
([CI run 43678639983](https://github.com/particlesector/retro-silicon-emulator/actions/runs/43678639983)).
The CI desktop job also verifies the extracted Release package using the frozen
combined fixture; virtual display/audio remains distinct from physical playback.

The selected Linux endurance environment is Ubuntu 24.04.4 through WSLg 1.0.66
(WSL 2.6.1, kernel 6.6.87.2-microsoft-standard-WSL2) on the same Windows host.
It uses SDL 3.4.14, X11, PulseAudio and Mesa 25.2.8 software Vulkan (llvmpipe,
LLVM 20.1.2), not a native Linux GPU driver. Restarting WSL restored initially
missing display/audio sockets. The first five-second check failed with one
starvation request and 161,781 us maximum step work; the 30-second retry passed.
The first long session failed audible playback: the user heard silence with
occasional glitches, WSLg logged RDP sink queue overruns, and monitor captures
returned no samples. A separate `paplay` reference tone was also inaudible.
The stopped session reported 489,936 elapsed ms but only 77,169 simulated ms,
despite zero SDL starvation requests. This is retained as a failure, not a pass.
Restarting WSLg's PulseAudio service restored the reference tone, confirmed by
the user. The fresh emulator run initially sounded continuous, then glitched
again. It was stopped at 102,979 elapsed ms with 46,020 simulated ms and zero
starvation requests; WSLg again logged queue overruns. A recovered monitor
capture contained varying PCM, confirming delivery into the bridge before
the failure. A subsequent independent 120-second `paplay` tone sounded
continuous to the user, but the player failed to finish draining within its
150-second timeout (exit 124). These observations do not isolate the exact root
cause or establish successful Linux endurance. WSLg's reported
[pause/resume issue](https://github.com/microsoft/wslg/issues/1392) is a possible
lead, not a confirmed diagnosis. A standalone SDL tone probe, with the same
pause/clear/resume pattern but no emulation or Vulkan, also glitched and advanced
only 40,916 ms of generated PCM in 90,004 ms. This reproduces the failure outside
the emulator core; it does not identify which SDL/WSLg component is responsible.
At the user's request, Linux endurance is on hold until testing on native Linux.
No WSLg result is accepted as a substitute, and the Linux gate remains open.
Logs are under
`build/m8/wslg-600s.*.log` and `wslg-recovered-600s.*.log` on the test host.

The first Windows session ran for 600,001 ms without a crash but failed the
strict zero-starvation check: 36,056 frames, 18 unsatisfied SDL requests,
599,941 simulated ms and a maximum frame work interval of 13,451 us. It overlapped
local builds and WSLg diagnostics. An instrumented quiet retry also failed:
18 requests between SDL ticks 383,841 and 384,011, a maximum loop gap of 291,496 us,
maximum whole-step work of 40,025 us and maximum frame work of 10,122 us. Both
results are retained as failures. Timing diagnostics include maximum whole-step,
event-pump, sleep and loop gaps plus first/last starvation ticks. Logs are under
`build/m8/windows-600s.*.log` and `windows-quiet-600s.*.log` on the test host.
The verified candidate requests high priority for the Windows audio-producing thread
only, with a nonfatal fallback if the OS rejects it. This is not a real-time or
system-wide policy. The extracted Release package at `6335078` passed its
600,001 ms run with Vulkan/WASAPI on Windows 11 Pro build 26200, an AMD Ryzen AI
Max+ 395 and Radeon 8060S (driver 32.0.31032.1003), using SDL 3.4.14 and MSVC 19.44.
It produced 36,068 frames and 28,806,774 samples (600,141 simulated ms), with zero
starvation requests, two loads, one reset, three expected errors and five GPU
readbacks. Peak audio/video queues were 13,116 bytes/12 frames; maximum frame,
whole-step, loop-gap, event-pump and sleep times were respectively 6,779, 38,602,
40,720, 3,271 and 3,368 us. The OS accepted high thread priority. Logs are
`build/m8/windows-priority-600s.*.log`. The Linux endurance gate remains open.

Subsequent Super Mario Bros. playtesting reported wrong colors and tearing.
The frontend candidate replaces the synthetic hue wheel with an explicit
64-color display palette and enables VSync. All 17 Windows Release suites pass.
Its 30-second Vulkan/WASAPI smoke check produced 1,812 frames in 30,012 ms,
30,146 simulated ms, zero starvation and five successful readbacks
(`build/m8-fixes/windows-vsync-30s.*.log`). This candidate has not repeated the
ten-minute endurance check or received visual gameplay confirmation. A local
ROM-only diagnostic observed 24-frame timer decrements during active Mario
play; chip clocks and frozen observations were not changed. No game assets
were added to the repository.

The user confirmed the corrected colors and absence of tearing, then reported
input lag. Instrumentation measured 141,297 us from the smoke key injection to
selection of its changed-color frame. Reducing the steady audio target from
120 ms to 50 ms, while retaining the larger startup/resize buffer, reduced the
observed interval to 79,251 us. VSync remains enabled. All 17 Release suites pass;
the 30,000 ms Windows smoke run reported zero starvation, five readbacks and
29,946 simulated ms (`build/m8-latency/low-latency-30s.*.log`). This measures
frontend frame selection, not physical input-to-display latency. Gameplay
confirmation and a new ten-minute Windows run remain pending for this candidate.

The 50 ms candidate was superseded by an independent frontend producer with
roughly 1 ms advancement chunks, a negotiated one-device-period + 2 ms PCM
target, and one latest completed frame. Core clocks and frozen captures remain
unchanged; VSync and the corrected palette are retained. All 18 Windows Debug
and Release suites pass, including the producer lifecycle/replay tests.
The 30-second run measured four input responses at 27,721-28,976 us (mean
28,243 us) with zero starvation. The extracted Release package then passed
600,001 ms: 36,050 frames, 599,857 simulated ms, 28,793,142 samples, zero
starvation, two loads, one reset, three expected errors and five readbacks.
Its source target was 1,152 bytes (12 ms), peak source queue 1,246 bytes,
and peak pending video one frame. Maximum chunk work was 1,782 us; even a
978,823 us event-pump stall did not starve audio. The scripted 150 ms GUI-block
check also passed. Four responses in this run measured 30,434-32,295 us (mean
31,401 us). These small samples are software observations, not a guaranteed
25 ms physical button-to-screen result. The user confirmed the Windows gameplay
feel, corrected colors and tear-free presentation. Linux endurance remains on
hold pending native Linux testing. Logs are
`build/m8-responsive/windows-responsive-600s.*.log`. The tested executable's
SHA-256 is `86c3b882af2517a5a244f2b91b9b44807f4dde7932f78ee37e3ea1270ee05b07`.
