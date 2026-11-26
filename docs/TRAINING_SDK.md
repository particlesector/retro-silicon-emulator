# Training and policy playback

The optional Python SDK provides `rse_training.EmulatorEnv` and a PPO,
random, or scripted runner. The SDK uses the in-process C++ automation
bridge. It supports one Gymnasium environment per adapter. The
C++ batch API remains available to native clients.

## Set up and run

Use a Python virtual environment. Install the pinned PyTorch wheel with the
CUDA 12.8 index for the RTX 5080 workstation, or the CPU index for development,
then install the remaining pinned packages.

```sh
python3 -m venv build/training-venv
. build/training-venv/bin/activate
python -m pip install torch==2.7.1 --index-url https://download.pytorch.org/whl/cu128
python -m pip install -r bindings/python/requirements-training.txt
cmake -S . -B build/training -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DRSE_BUILD_PYTHON=ON -DRSE_TRAINING_TESTS=ON -DPython3_EXECUTABLE="$VIRTUAL_ENV/bin/python"
cmake --build build/training
ctest --test-dir build/training --output-on-failure
build/training/rse-acceptance-tests --write-set build/training/fixtures
```

For CPU wheels, use `https://download.pytorch.org/whl/cpu` in the PyTorch
install command. The production C++ build does not depend on the SDK or any
of its Python packages. [PyTorch wheel installation](https://pytorch.org/get-started/previous-versions/)

```sh
python bindings/python/rse_train.py \
  --bridge build/training/librse_python_bridge.so \
  --rom build/training/fixtures/rse-loop-v1.nes \
  --profile bindings/python/profiles/nrom-smoke.json \
  --out build/training/ppo-smoke --device cuda --seed 19 \
  --steps 128 --rollout 32 --checkpoint-every 64 --build-sha "$(git rev-parse HEAD)"
```

On Windows, pass the absolute path to the built `rse_python_bridge.dll`.
Use `--device cpu` for the development smoke. `--device cuda` fails if
CUDA is unavailable or cannot execute an arithmetic kernel. The run
manifest records the actual PPO device, GPU model, compute capability,
CUDA runtime, driver version when available, and peak PyTorch CUDA
allocation. Verify that `gpu_name` identifies the target RTX 5080.

Use `--method random` or `--method scripted --script-action 0` for baseline
runs. Use `--resume <prior-run>/checkpoint.zip` with a new output directory
to continue PPO. The neighboring JSON must match the profile, ROM, bridge,
seed and rollout. Load trusted checkpoints only. Resume restores the policy
and optimizer with a fresh environment and rollout. It does not reproduce
the exact continuation of an in-flight episode or GPU RNG stream.
Periodic checkpoints use distinct timestep suffixes. A completed run also
writes `checkpoint.zip`.

`manifest.json` records dependencies, device, seeds, artifact hashes and
timestep settings. `metrics.jsonl` contains episode returns and lengths, and
PPO update metrics. PPO finishes the current rollout, which can exceed the
requested number of steps. The fixture increments a RAM counter independent
of input. It exercises the training path without claiming learned gameplay.

## Environment contract

The profile validates ROM SHA-256, action names and the inspected address
ranges before use. Actions are indexed complete pressed-button sets.
`reset(seed=..., options=...)` selects from the declared warmup ticks using
the Gymnasium RNG, or accepts an explicit allowed `warmup_ticks` option.

`step(action)` applies the pressed set on the first tick and holds it for
`ticks_per_repeat * action_repeat` master ticks. The default interval is the
nominal number of NTSC master ticks per full frame. A step can cross a
different number of frame publications depending on phase and PPU timing.
The environment reports the actual tick and latest frame number. GPU use
is confined to the trainer, and native emulated audio is advanced and drained.

- `screen` returns an owned H×W×3 RGB `uint8` array of the latest published
  frame, or a black array before the first publication.
- `memory` returns an owned flat `uint16` array of configured 8-bit reads.
  Value 256 represents an unavailable address.
- `hybrid` returns both arrays in a Gymnasium dictionary.

Task rules live in the Python profile and evaluator. A successful condition
sets `terminated`. Reaching the configured step limit without success sets
`truncated`. Deterministic replay compares observations, reward, flags and
integer timing for the same seed and actions. GPU learning results can vary
across devices and drivers. [Gymnasium environment contract](https://gymnasium.farama.org/api/env/)

## Signal Run experiment

Use [signal-run-v1.json](../bindings/python/profiles/signal-run-v1.json)
with its pinned ROM SHA-256. Keep this protocol and its source revision
fixed for training and evaluation.

- Train memory, screen, and hybrid PPO for 16,384 timesteps per mode at
  each of seeds 101, 202, and 303. Use 256-step rollouts, minibatches of 128,
  four PPO epochs per update, and two 64-unit policy/value layers.
- Draw training starts from cells 0, 2, 4, 6, and 8. Evaluate on cells
  1, 3, 5, 7, and 9, cycling through them for 20 episodes each. Episode
  seeds are 1000–1099. Each start is constructed by a scripted prefix from
  a fixed power-on state after 757,368 warmup ticks. The prefix is outside
  the episode reward and step budget. Held-out positions can still be visited
  during training. PPO evaluation repeats each deterministic start 20 times,
  so the counts describe five start positions.
- Allow 20 steps after resetting to a start. An action holds its button set
  for 714,736 master ticks, with one game update per two PPU frames. Use
  reward 0.15 per point gained, -0.10 per collision, -0.01 per step, and
  +1 for completion.
- Memory is a 12-value one-hot position. Screen is a normalized RGB vector
  sampled at rows 91 and 99 and columns 19, 35, …, 195 of the latest
  published frame. Hybrid combines both. The screen policy does not receive
  RAM or diagnostic info. Every mode uses the same reward and stop rules.
- Evaluate deterministic PPO actions, uniform random actions, and a scripted
  policy that jumps only at the obstacles. Each of the three memory-policy
  seeds must complete at least 80/100 episodes and beat random. Report the
  results of all nine trained policies, including failures, and the scripted
  baseline. The comparison does not require all observation modes to meet
  the memory-policy threshold.

Generate the project-owned game and run the fixed-budget experiment in the environment configured above.

```sh
build/training/rse-gameplay-tests build/training/signal-run
python bindings/python/rse_gameplay.py --bridge build/training/librse_python_bridge.so --rom build/training/signal-run/signal-run-v1.nes --out build/training/experiment --device cuda --build-sha "$(git rev-parse HEAD)"
```

The runner verifies CUDA execution before training. It writes the device and
dependency manifest, individual episode results, model SHA-256 values, and
`summary.json`. A failed acceptance threshold produces a nonzero exit code
and keeps the measured results. Preserve the logs and summarize completion
counts, mean rewards, and failures by observation mode and seed. Keep ROMs,
checkpoints and generated replay files out of Git. `--baselines-only` verifies
the baselines without training.

## Automated failure reproduction

`rse-gameplay-tests` asserts that walking into the first obstacle leaves
the player at cell 2 and records a collision. A deliberately defective
ROM instead moves to cell 3 without a collision. The test replays the
same input into the defective ROM at different advancement chunk sizes
and compares canonical snapshots. The optional output directory also
receives `signal-run-defect-v1.nes` and
`signal-run-obstacle-failure-v1.resl`, which binds to that defective ROM.
This is a reproducible gameplay assertion, with no claim about general
emulation accuracy.

The generated failure log SHA-256 is
`7f13f747d274d282592f2fbac3ee3eab33edf41eeaa01cd69e3cbffbb348c7e5`.

## Native ONNX policy playback

The player accepts a versioned bundle for the Signal Run v1 **memory** PPO policy.
The exporter reads a trusted local Stable-Baselines3 checkpoint and verifies
its SHA-256 and the pinned ROM SHA-256. Playback uses C++ ONNX Runtime and
does not import Python.

## Build and export

Use ONNX Runtime 1.22.0 C/C++ headers and libraries. Set
`-DRSE_BUILD_POLICY=ON -DRSE_ONNXRUNTIME_ROOT=/absolute/path/to/sdk` when
configuring CMake. The SDK root should contain `include` and `lib`.
For a split SDK layout, set `RSE_ORT_INCLUDE` and `RSE_ORT_LIBRARY` directly,
and `RSE_ORT_DLL` on Windows. Build with `RSE_BUILD_DESKTOP=ON` to include
the real-time window. Keep the SDK runtime libraries available to the
dynamic loader. The optional build does not redistribute CUDA libraries.
[ONNX Runtime C++ installation](https://onnxruntime.ai/docs/get-started/with-cpp.html)

Installation with policy support includes `rse-policy`. On Windows place
the matching SDK's `onnxruntime.dll` beside the installed executables so
Windows selects it ahead of older system copies. On Linux use
`LD_LIBRARY_PATH` to point to the SDK runtime directory. Installed policy
executables require an external ONNX Runtime.

For native Ubuntu GPU verification, use the CUDA 12 C++ SDK and make its CUDA
and cuDNN 9 libraries available to the dynamic loader. Use the GPU SDK even
when verifying its CPU provider. `cuda` requires CUDA and disables CPU
operator fallback and TF32. `auto` tries CUDA and falls back to CPU if initialization
or parity fails. The headless and desktop reports name the selected provider.
If the binary SDK lacks kernels for the RTX 5080, build the official source
release with CUDA 12.8, cuDNN 9, and `CMAKE_CUDA_ARCHITECTURES=120`.
[CUDA provider requirements](https://onnxruntime.ai/docs/execution-providers/CUDA-ExecutionProvider.html)

The Microsoft Linux GPU NuGet package at version 1.22.0 is an alternative
SDK source. Extract `buildTransitive/native/include/` into the SDK's
`include/` directory and `runtimes/linux-x64/native/` into `lib/`. If the
package supplies only `libonnxruntime.so`, create a `libonnxruntime.so.1`
symlink to it so the dynamic loader can resolve its SONAME. Retain the
package's notices and verify the CUDA provider's libraries with `ldd`.
[Microsoft Linux GPU package](https://www.nuget.org/packages/Microsoft.ML.OnnxRuntime.Gpu.Linux/1.22.0)

Use the M17 training environment for export and install `onnx==1.18.0` in it.
Generate the ROM with `rse-gameplay-tests <output-directory>` if necessary.
Use one of the trained memory checkpoints from M17 and its recorded digest.

```bash
python -B bindings/python/rse_export_policy.py \
  --checkpoint "$checkpoint" --checkpoint-sha256 "$digest" \
  --rom "$rom" --out "$bundle"
```

Capture the export JSON. It includes the model and bundle SHA-256, source
checkpoint, ROM identity, tensor shapes, preprocessing and action mapping.
Use `RSE_BUILD_PYTHON=ON` and `RSE_TRAINING_TESTS=ON` with the same Python
environment to enable the optional exporter and playback regression test.

## Playback and replay

```bash
rse-policy "$rom" "$bundle" cpu "$out/headless-cpu.resl"
rse-policy "$rom" "$bundle" cuda "$out/headless-cuda.resl"
rse-desktop "$rom" --model "$bundle" --provider cuda --replay "$out/desktop.resl" --seconds 8
rse-policy --replay "$rom" "$out/desktop.resl"
```

Use fresh replay paths. With no manual input, confirm the headless and
desktop reports show completion. Compare the `state_hash` emitted by
desktop with the hash from its headless replay. Repeat desktop playback
and press a controller key before the policy finishes to verify manual
takeover. Any nonempty human input takes control through releases until
reset. Initial empty input leaves the policy in control. The desktop saves
the current timeline on orderly exit. A reset or replacement starts a new
timeline. Inference runs synchronously on the playback producer and can
delay presentation, while the machine advances through every requested tick.

The controller warms up to tick 757,368 and decides at intervals of 714,736
ticks. It reads the current position from CPU RAM `$10`, encodes it as
float32 one-hot input `position[1,12]` and chooses the first maximum from
`logits[1,3]`. Actions are release, Right, and Right+A. The finish flag is
RAM `$12 == 1`. After completion or 20 decisions, the policy releases the
controller at the next decision. Both human and policy button sets are
recorded at their scheduled master ticks in an M14 `RESL` log.

## Version and parity

Bundle version 1 uses little-endian fields. Its header contains `RSEP`, a
16-bit version, the 16-bit Signal Run memory task ID (1), 64-bit FNV-1a
digests for the ROM and ONNX bytes, and a 32-bit ONNX length. The header
is followed by 36 float32 reference logits and the bounded ONNX payload.
Task ID 1 fixes the preprocessing and controller rules above. The runtime
rejects incompatible task IDs, tensor names, element types, shapes, lengths,
ROMs, payload digests and nonfinite outputs.

Every provider must match the training tool on all 12 possible input vectors.
Logits use an absolute tolerance of `1e-5 + 1e-4 * abs(reference)`. Selected
actions must agree exactly. The authored test checkpoint and its fixture
exercise parity, headless completion, manual takeover, and state-identical
replay.

For keyboard controls, trainer preview, stopping and resuming, see the [desktop workspace](DESKTOP.md#desktop-ai-workspace). The [release record](ROADMAP.md) links to the measured CPU and CUDA results.
