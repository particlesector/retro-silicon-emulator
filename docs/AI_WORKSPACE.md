# Desktop AI workspace

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

## Start a trainer

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

## Acceptance checks

Compare baseline and CPU-training runs with the same ROM, duration, display,
and audio driver. For a ten-second run, require active trainer steps and a
published preview, at least 9.5 seconds of simulated time, no audio starvation,
and a maximum UI loop gap below 100 ms and within 50 ms of the baseline.
Confirm the trainer saves a final checkpoint on cooperative stop.

The RTX 5080 repeat uses the same procedure with `--trainer-device cuda` and
records the driver, CUDA device, contention and any resource limit. The owner
deferred that hardware check to final M20 acceptance.

## CPU acceptance record

Windows Release, Vulkan and SDL dummy audio, using Signal Run and the
workspace profile. Both ten-second runs presented 601 frames and advanced
10,007 ms of emulated time with zero audio starvation requests.

| Run | Maximum UI loop gap | Trainer steps observed | Preview updates |
| --- | ---: | ---: | ---: |
| Baseline | 34.7 ms | — | — |
| CPU training | 40.3 ms | 687 | 21 |

The CPU trainer stopped at a rollout boundary and saved its checkpoint on
window exit. The first comparison exposed a 556 ms UI stall. Process
management and telemetry reads moved off the UI thread before the passing
comparison. Raw logs are in `build/m19/comparison-20261121-062142/`.
