# NTSC 2A03 APU (M6)

## Contract and reference plan

`audio::Rp2a03Apu` models the sound portion of the NTSC 2A03, separately from
the reusable 6502. One `clock(get_phase)` is one CPU slot, including DMA holds.
The system clocks the APU before the CPU bus access; register writes therefore
take effect after that slot's channel/frame clocks. Native register offsets
are $00-$13, $15 and $17; console address decoding belongs to the system.
Power-on is deterministic, not a claim about analog startup state.
Pulse/noise/DMC timers advance only on the supplied get phase; triangle advances
on every slot. Noise/DMC diagnostic timer values count APU ticks (half the
published CPU-period tables). The initial DMC divider is 213, giving its first
output tick on the 214th get phase; the frame sequence starts at cycle zero.

Before implementation, the acceptance cases are named as follows:

- `frame-sequence`: quarter/half clocks at 7457/14913/22371/29829 and
  four-step IRQ assertion at 29828-29830; five-step last clock at 37281.
- `channel-counters`: length table, enable gating, envelope loop/divider,
  pulse duties and sweep negate difference, triangle linear reload/held DAC,
  and both noise feedback taps with independently stepped sequences.
- `dmc-reader`: rate table, bit order, saturation, buffering, wrap at $FFFF,
  looping, IRQ clear rules, and buffered playback after disabling the reader.
- `dma-arbitration`: write-delayed halt, 3/4-slot DMC holds, mapped idle reads,
  DMC get priority over OAM get without lost OAM data, and continued chip clocks.
- `pcm-repeat`: fixed synthetic register/input timelines produce pinned 48 kHz
  PCM observations, unchanged by chunking, reset, draining, or host pacing.
- `desktop-stream`: bounded SDL playback with queue/underrun diagnostics;
  document the actual host and duration before checking desktop acceptance.

References are the NESdev hardware documentation for the
[frame counter](https://www.nesdev.org/wiki/APU_Frame_Counter),
[pulse](https://www.nesdev.org/wiki/APU_Pulse),
[triangle](https://www.nesdev.org/wiki/APU_Triangle),
[noise](https://www.nesdev.org/wiki/APU_Noise),
[envelope](https://www.nesdev.org/wiki/APU_Envelope),
[length counter](https://www.nesdev.org/wiki/APU_Length_Counter),
[DMC](https://www.nesdev.org/wiki/APU_DMC),
[DMA](https://www.nesdev.org/wiki/DMA), and
[mixer](https://www.nesdev.org/wiki/APU_Mixer).
Tests use project-owned generated data, with no external ROM assets.

## Audio and system boundary

The APU exposes channel DAC values and requests sample bytes without accessing
console memory. The 2A03 DMA arbiter halts reads, waits through writes, overlaps
DMC preparation with OAM work, and gives DMC gets priority. Halt/dummy/alignment
reads use mapped memory, preserving controller/PPU read side effects. APU frame
and DMC interrupts are ORed with the system's external IRQ source. Reading $4015
clears only frame IRQ; $4015 writes clear DMC IRQ, and $4010 IRQ-disable clears it.
Bit 5 of $4015 comes from CPU open bus. Disabled DMC playback retains its DAC and
already buffered output; length/status describes the reader, not audible output.

`NtscPcm` uses the NTSC CPU frequency 19,687,500/11 Hz and emits exactly 48,000
mono signed 16-bit samples per simulated second. Fractional box integration
splits CPU-slot contributions at sample boundaries. The mixer uses integer
versions of the documented two-table approximation: pulse numerator 95.52,
TND numerator 163.67 with index `3*triangle + 2*noise + dmc`. This is an
approximation of the nonlinear DAC, not an analog measurement. A Q15 DC blocker
uses coefficient 32384/32768. Integer division truncates toward zero. There are
no floating-point or host-clock dependencies in generated PCM.

Set `Profile::capture_audio` to retain samples, then call `Machine::take_audio()`
regularly. The default queue holds at most 48,000 samples; overflow faults the
machine until reset rather than dropping samples. Capture is disabled by default
for headless runs. Capture/drain settings never change chip clocks, DMA, or CPU
execution. Reset clears PCM filter phase and queued samples as well as the APU.

`nes-audio-tests` runs a generated five-channel NROM program for 2,000,000
master ticks. It produces 4,469 samples with FNV-1a hash `21e6c536d9b67d48`
(two little-endian bytes per signed sample). This is a regression capture,
separate from the independently specified channel/counter cases. Repeated,
chunked, drained, and reset runs must match; capture-disabled runs must preserve
CPU and PPU execution. The integrated program also overlaps looping DMC and OAM
DMA. Separate programs acknowledge frame/DMC IRQs through CPU register accesses.

## Desktop playback

Build the optional desktop target as described in the README, then run:

```sh
rse-desktop --audio example.nes
rse-desktop --audio example.nes --seconds 30
```

Escape or closing the window stops playback. This mode presents no emulated
video. SDL converts the fixed 48 kHz source to the selected device format.
The producer maintains roughly 120-137 ms of queued source audio and waits when
the queue is full. Host pacing only determines when the next fixed simulated
block is computed. A diagnostic record reports driver, elapsed time, source
sample count/hash, peak queue bytes, and unsatisfied SDL callback requests.
A bounded run fails if any such starvation request occurs. This checks the SDL
source stream, not electrical speaker output or subjective sound quality.

Generate the project-owned smoke fixture with a built test executable:

```sh
rse-nes-audio-tests --write-smoke audio-smoke.nes
rse-desktop --audio audio-smoke.nes --seconds 30
```

On Windows use the executable paths under the chosen build directory and make
SDL3.dll available beside the executable or on PATH. No fixture ROM is committed.

## Verification limits

Only the NTSC 2A03 variant is implemented. Analog startup noise, analog filter
and DAC equivalence, PAL, expansion audio, and silicon-specific DMC stop/abort
bugs are not verified. Frame/register behavior is defined at CPU-slot resolution;
subcycle write collisions (including length reload at a frame clock and $4011
DAC conflicts) need additional hardware-derived cases before stronger claims.
The existing PPU/CPU compatibility limits still apply. M7 remains responsible
for emulated video presentation, input UI, and combined audio/video pacing.

## Validation

Windows 11 (build 26200), AMD Ryzen AI Max+ 395, MSVC 19.44, SDL 3.4.14:
CMake configure/build and all 14 CTest suites pass in Debug and Release, including
the unchanged CPU and PPU reference suites. A Release playback run on November 14,
2026 used the generated five-channel fixture and WASAPI for 30,000 ms, with zero
unsatisfied SDL requests and a peak source queue of 12,844 bytes. It generated
1,445,622 samples (including prebuffer/lookahead); the source hash was
`c9be5a38b715e684` after the divider-phase correction. Earlier 30-second runs
also reported zero starvation.
These are stream diagnostics; audible output was not independently assessed.
See [Desktop verification](DESKTOP.md#validation) for combined frontend smoke
checks. Physical Linux audio/gamepad acceptance remains an M8 gate.

Linux Debug/Release and ASan/UBSan with leak detection pass all 14 suites in
[CI run 43655211909](https://github.com/particlesector/retro-silicon-emulator/actions/runs/43655211909).
Linux CI uses the frontend-free build. Windows builds include the desktop target;
no SDL/Vulkan dependency was added to the chip, system, or CLI libraries.
