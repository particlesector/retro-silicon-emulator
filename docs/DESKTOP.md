# Desktop MVP (M7)

## Contract and reference plan

The desktop owns cartridge selection, Vulkan presentation, SDL
input/audio devices, and real-time pacing. It does not modify chip clocks or
instruction behavior. SDL3's explicitly selected `vulkan` renderer uploads the
session's completed RGBA frame; failure to obtain that backend is an error, not a silent
fallback. Display output uses a documented approximate RGB palette and 4:3 aspect
ratio with letterboxing. This is not composite-video or analog color emulation.

The optional [native policy player](MODEL_PLAYBACK.md) uses the same desktop
producer and input path for autonomous play and manual takeover.

Acceptance cases, specified before implementation:

- `desktop-policy`: palette/emphasis conversion, 4:3 viewport bounds, input
  combination/focus release. Latest-frame handoff and reset/replacement are
  covered by `desktop-playback-tests`.
- `desktop-replay`: the same frame-boundary input sequence and frame count
  produce identical machine state, frames, and PCM under different host budgets.
- `desktop-smoke`: a project-owned generated NROM fixture exercises visible
  output, audio, keyboard, a virtual standard SDL gamepad, focus release, reset,
  cartridge replacement/error recovery, resize, and normal close. Vulkan readback
  checks the actual output and letterbox regions; report audio starvation.
- `release-package`: install and archive the Release app with SDL3 and license
  notices, then run the extracted package on Windows and Linux. Linux CI may use
  Xvfb, lavapipe, and dummy audio; record this boundary rather than claiming a
  physical Linux GPU, controller, or sound device was tested.

The M1-M6 reference suites must continue to pass. No game ROMs or proprietary
firmware are used by the smoke tests. The completed M8 endurance
and frozen-set acceptance record is linked below.

## Use

Run `rse-desktop` and press **O** (or Ctrl+O) to choose a cartridge. A `.nes` file
can also be dropped into the window, or supplied on the command line:

```sh
rse-desktop example.nes
```

Only the documented mapper-0 iNES subset is accepted. Missing, malformed, or
unsupported files produce an error; opening another cartridge remains possible.
Failed cartridge selection preserves the running cartridge. **F5** reconstructs
its deterministic power-on state and clears queued audio/video and input. **Esc**
or the window's close button exits. Minimizing pauses emulation and playback;
restoring resumes. File dialogs and error messages also pause playback.

| NES button | Keyboard | Standard SDL gamepad |
| --- | --- | --- |
| A | X | South (A on Xbox) |
| B | Z | West (X on Xbox) |
| Select | Right Shift | Back/View |
| Start | Enter | Start/Menu |
| Directions | Arrow keys | D-pad |

The first connected standard SDL gamepad supplies controller 1. Keyboard and
gamepad bits combine with OR; simultaneous opposite directions are preserved.
Focus loss clears keyboard state and suppresses input until focus returns.
Gamepad removal releases its bits. There is no controller-2 or remapping UI.

## Presentation and pacing

Frame color/emphasis samples are converted to an opaque RGBA texture using the
NES adapter's explicit 64-color NTSC display approximation in
`include/rse/systems/nes/palette.hpp`.
This replaces the original synthetic hue/luma wheel; it is still not a measured
composite decoder. Emphasis approximately attenuates the other RGB channels.
Letterboxing
preserves a 4:3 display ratio at any pixel size; nearest-neighbor sampling is
used. Only new visible frames or expose/resize events trigger presentation.
SDL handles Vulkan swapchain recreation. The app requires SDL's `vulkan`
backend and enables VSync by default. Set `SDL_RENDER_VSYNC=0` before
launch to skip refresh synchronization. This may produce tearing.

The audio source is M6's fixed 48 kHz PCM. A frontend producer thread advances
the machine in 21,480-master-tick chunks (about 1 ms), sampling current input
before each chunk. It targets one negotiated audio-device period plus 2 ms of
source PCM: 12 ms on the tested Windows WASAPI device. This is not a universal
device latency setting. A slow host can still underrun.

The producer owns all emulation work. A mutex serializes inspection, input,
load/reset and pause commands; the core is never accessed concurrently. No
GPU or event APIs run on the producer. Only this thread requests best-effort
high priority on Windows; the process and system priorities are unchanged.
It keeps one latest completed video frame, replacing stale frames while the
GUI is busy. Rendering remains on the main thread. Audio generation
therefore continues across refresh, resize and readback waits. Pause/load/reset
stop the producer before pausing/clearing the stream; resume waits for priming.
The producer joins before its stream is destroyed, and execution faults are
reported on the GUI thread without discarding a working cartridge on load error.

The short source queue bounds the newest frame's lead over consumed source PCM;
SDL/device buffering and display scanout add their own delays. This is not a
measured speaker/display latency guarantee. Vsync does
not set chip clocks. For example, Super Mario Bros.' displayed time units are
shorter than seconds; do not slow the machine to make that counter match a clock.
Slow hosts can cause reported audio starvation; they do
not skip emulated cycles. Work per iteration is capped to keep events responsive.
Input changes are scheduled at the next master tick of the next producer chunk.
Already executed ticks and queued PCM are not retroactively changed.
The smoke report's `input_frame_latency_us` is the first injected keyboard
response. `input_min_us`, `input_max_us` and `input_mean_us` cover four keyboard/
gamepad press/release color responses observed by the GUI after presentation.
They exclude physical keyboard, compositor and display scanout latency; they
are not end-to-end measurements. All four responses are required. The script
also blocks the GUI for 150 ms and checks continued audio production.

`desktop-playback-tests` (available with the SDL desktop build) verifies paused
state/RAM/PCM-clock equivalence with the headless machine, pause/reset, fault
recovery and teardown. Its real-time blocked-GUI/zero-starvation check runs only
in optimized builds; unoptimized builds retain the deterministic lifecycle checks.

## Release package

Configure the desktop with the README's requirements. Supply SDL3's license
path, which is required to redistribute its runtime. For example on Windows:

```powershell
cmake -S . -B build/vs -G "Visual Studio 17 2022" -A x64 -DRSE_BUILD_DESKTOP=ON -DSDL3_DIR=C:/sdl3/SDL3-3.4.14/cmake -DRSE_SDL_LICENSE=C:/sdl3/SDL3-3.4.14/LICENSE.txt
cmake --build build/vs --config Release
ctest --test-dir build/vs -C Release --output-on-failure
cmake --build build/vs --config Release --target PACKAGE
```

On Linux:

```sh
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DRSE_BUILD_DESKTOP=ON -DCMAKE_PREFIX_PATH=/path/to/sdl-install -DRSE_SDL_LICENSE=/path/to/SDL/LICENSE.txt
cmake --build build/release
ctest --test-dir build/release --output-on-failure
cmake --build build/release --target package
```

The ZIP appears in the build directory. Extract it and run `rse-desktop` from
the extracted folder. Windows includes SDL3 and the MSVC runtime DLLs; Linux
includes SDL3 under `lib` with a relative runtime search path. Both include
project/SDL license notices and documentation. The host supplies the Vulkan
loader/graphics driver, window system, audio backend, and ordinary OS libraries;
these packages are not distribution-independent Linux static binaries.
The Vulkan development SDK is needed for building, not for end-user deployment.

## Reproduce the smoke test

Use a built test executable to generate the valid fixture plus its malformed
and unsupported companions, then run the packaged executable:

```sh
rse-desktop-policy-tests --write-smoke desktop-smoke.nes
rse-desktop --smoke desktop-smoke.nes --seconds 30
```

The program includes five-channel audio, a visible solid color, and an NMI
controller reader at RAM $10. The smoke script injects keyboard/focus/drop/close
events, attaches a virtual standard SDL gamepad, resets, minimizes/restores, and
resizes to wide and tall windows. Vulkan readback compares actual texture colors
and black letterbox corners. Expected load errors are printed. Failure of any
assertion or any unsatisfied SDL audio request makes the test fail. The test
does not establish physical gamepad compatibility or subjective audio quality.

## Validation

See [M11 validation](M11_VALIDATION.md) for the session migration, comparison
procedure, current package checks and outstanding native audio approval.

The [accepted MVP baseline](MVP_ACCEPTANCE.md#accepted-baseline) records the
Windows endurance counters, native Ubuntu manual approval, response-measurement
limits and frozen fixtures. [Historical validation](MVP_VALIDATION_HISTORY.md)
retains the earlier M7/M8 results and failures without redefining current pacing.

The current desktop build has 19 CTest suites. `desktop-playback-tests` checks
the actual latest-frame handoff, initial all-buttons input, reset/replacement,
headless equivalence and fault recovery. The extracted smoke driver lives in
`apps/desktop/smoke.cpp`; its internal `SmokeHost` view exposes observations and
commands rather than owning window, gamepad or machine handles. Smoke response
metrics are kept outside application orchestration. Timing policy values are
named in `playback_timing.hpp`; cleanup does not change them.

### M9 cleanup validation

On November 15, 2026, all 18 suites passed in Windows/MSVC Debug and Release
and local Ubuntu/GCC Release. The new initial `0xff` input regression failed on
the M8 implementation before the sentinel fix; reset/replacement input and latest
frame checks now pass. Frozen fixture bytes/captures and CLI behavior are unchanged.

The Windows Vulkan/WASAPI 30-second smoke run reported zero starvation, five
readbacks, 29,876 simulated ms in 30,000 elapsed ms and four input responses
averaging 32,513 us. In a subsequent same-host five-second comparison, the
accepted M8 executable averaged 24,584 us and the extracted M9 package averaged
22,736 us; both had zero starvation and a 1,152-byte source target. These are
small software measurements subject to refresh/host variation, not evidence of
a new physical latency guarantee. Logs are under `build/m9/windows-smoke.*.log`,
`baseline-5s.*.log` and `package-5s.*.log` on the test host.

The extracted Linux Release package passed the 30-second Xvfb/Openbox/lavapipe
flow with dummy audio, zero starvation, five readbacks and four input responses.
The six-second interruption test still failed with the expected early-exit
message. Logs are `build/m9/linux-package.*.log` and `linux-early-exit.log`.
These are virtual-display checks; the native Ubuntu owner approval in the
accepted baseline remains M8 evidence, not a new physical M9 test.
