# Desktop MVP (M7)

## Contract and reference plan

The desktop owns cartridge selection, RGB conversion, Vulkan presentation, SDL
input/audio devices, and real-time pacing. It does not modify chip clocks or
instruction behavior. SDL3's explicitly selected `vulkan` renderer uploads the
completed 256x240 frame; failure to obtain that backend is an error, not a silent
fallback. Display output uses a documented approximate RGB palette and 4:3 aspect
ratio with letterboxing. This is not composite-video or analog color emulation.

Acceptance cases, specified before implementation:

- `desktop-policy`: palette/emphasis conversion, 4:3 viewport bounds, input
  combination/focus release, and frame selection against consumed PCM positions.
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
firmware are used by the smoke tests. M8's ten-minute physical-host acceptance
session and frozen compatibility set remain separate gates.

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
explicit 64-color NTSC display approximation in `apps/desktop/presentation.hpp`.
This replaces the original synthetic hue/luma wheel; it is still not a measured
composite decoder. Emphasis approximately attenuates the other RGB channels.
Letterboxing
preserves a 4:3 display ratio at any pixel size; nearest-neighbor sampling is
used. Only new visible frames or expose/resize events trigger presentation.
SDL handles Vulkan swapchain recreation; the app requires its `vulkan` backend
and synchronized presentation (VSync) to avoid tearing.

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
GUI is busy. Rendering remains on the main thread with VSync. Audio generation
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

All 15 CTest suites pass in Windows/MSVC and Linux/GCC Debug and Release;
Linux ASan/UBSan with leak detection also passes. Chip reference fixtures remain
unchanged. The desktop replay verifies 120 frames of identical CPU/frame/PCM
observations across different advancement chunks.

On November 14, 2026, an extracted Windows Release ZIP ran for 30 seconds on
Windows 11 build 26200, AMD Ryzen AI Max+ 395 / Radeon 8060S, SDL 3.4.14 and WASAPI.
It generated 1,801 frames, made two successful loads and one reset, recovered
from three expected file errors, passed five Vulkan readback checks, and reported
zero starvation. Startup buffering was then increased to 120-137 ms to cover
software Vulkan initialization; subsequent Windows smoke checks also pass.
The native Windows picker, visible emulated frame, reset/exit, and readable
welcome screen were inspected interactively.

The extracted Linux Release ZIP passed the same 30-second flow in
[CI run 43672447350](https://github.com/particlesector/retro-silicon-emulator/actions/runs/43672447350):
Ubuntu 24.04.5, SDL 3.4.14, Xvfb/Openbox, Mesa 25.2.8 lavapipe (llvmpipe), dummy
audio and a virtual standard gamepad. It generated 1,823 frames, passed five
Vulkan readback checks, and reported zero starvation with a peak source queue
of 13,116 bytes. Linux native GPU, physical audio/gamepad, Wayland, and native
file-portal behavior are not established by this virtual-display check.
M8 retains the frozen compatibility set and ten-minute physical-host sessions.
