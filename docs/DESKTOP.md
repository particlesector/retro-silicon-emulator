# Desktop playback

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

## Presentation and audio

SDL3 uses its Vulkan renderer to present the latest completed frame at the aspect ratio supplied by the session. The NES adapter supplies an approximate NTSC palette and 4:3 aspect. The producer advances the machine in roughly 1 ms chunks and targets one audio device period plus 2 ms of queued source PCM. The GUI can wait for presentation while emulation continues. VSync defaults on. The M20 RTX 5080 training comparison used `SDL_RENDER_VSYNC=0`, which can introduce tearing.

## Audio-only playback

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

## Desktop AI workspace

Press **F1** during normal desktop playback to show the workspace. The left
side shows the running emulator. The right side shows the selected task,
policy, checkpoint, controller buttons and inspected RAM. A configured
trainer adds a separate preview and live metrics.

| Key | Control |
| --- | --- |
| O | Choose a cartridge |
| F2 | Choose a JSON task profile |
| F3 | Load an ONNX policy bundle for the selected ROM |
| F4 | Select a trusted local PPO checkpoint |
| F5 | Reset the running emulator and return control to a loaded policy |
| F6 | Start a new training run |
| F7 | Cycle profile default, memory, screen, and hybrid observations |
| F8 | Request a stop after the current PPO update |
| F9 | Resume from the selected checkpoint or the last successful run |
| F10 | Unload a policy selected in the workspace |
| F11 | Inspect the next byte in NES RAM `$10`–`$13` |

Policy loading requires `RSE_BUILD_POLICY=ON` and runs asynchronously. An
invalid or incompatible bundle leaves the current session running. A model
selected at startup with `--model` keeps its original command-line replay
workflow. A controller press takes over from the policy until reset.

### Start a trainer

Build the desktop and Python bridge with `RSE_BUILD_DESKTOP=ON` and
`RSE_BUILD_PYTHON=ON`. Install the [training SDK](TRAINING_SDK.md)
dependencies in a Python environment. To use Signal Run, generate the ROM
with `rse-gameplay-tests` and select the workspace example profile.

```bash
rse-desktop "$rom" --workspace \
  --trainer-python "$python" --trainer-script "$PWD/bindings/python/rse_train.py" \
  --trainer-bridge "$bridge" --trainer-out "$PWD/build/trainer-runs" \
  --trainer-profile "$PWD/bindings/python/profiles/signal-run-desktop.json" \
  --trainer-device cpu --trainer-steps 16384
```

Pick a profile and use F6, or pass `--trainer-autostart` to launch the
configured run with the window. Training uses one environment in a child
process. Switching ROM or task in the workspace affects the next run.
The active trainer continues with the settings it started with. Use
`--trainer-observation` to set the initial override. The trainer defaults to
CPU, and the CLI training and policy players remain available.

The child publishes only its latest telemetry and RGB frame, at most four
times per second during rollout. The workspace shows completed steps,
steps/s, recent episode reward, loss, current discrete training action and
the latest preview. A missing reward or loss is shown as `--`. Each run
keeps its own manifest, metrics and checkpoints. If the process exits with
an error, the emulator keeps playing and the last successful checkpoint
remains available. Close requests cooperatively stop the child and allow
up to eight seconds for shutdown before forcing termination. A resumed run
restores the model and optimizer at a fresh rollout.
