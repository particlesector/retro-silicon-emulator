# M20 native Ubuntu RTX 5080 acceptance

Run from the repository root on the native Ubuntu 24.04 desktop. Use the M20
branch `feature/m20-ai-mvp-acceptance` and record its exact commit. Keep
generated ROMs, checkpoints and raw
logs under the ignored evidence directory. Commit the evidence summary and
roadmap updates after recording the results.

## Prepare the workstation

Install CMake, Ninja, a C++23 compiler, Python with venv, SDL3 and the Vulkan
development files. Use SDL3 3.4.14 as pinned in the [Linux packaging
workflow](../.github/workflows/ci.yml). Set `sdl_prefix` and `sdl_license` to
the SDL install prefix and source license. Set `ort` to the CUDA-enabled
ONNX Runtime 1.22.0 SDK root and expose its CUDA 12 and cuDNN 9 libraries.
See [model runtime setup](MODEL_PLAYBACK.md#build-and-export).

```bash
set -euo pipefail
mkdir -p build
run=$(mktemp -d "$PWD/build/m20-ubuntu.XXXXXX")
git rev-parse HEAD | tee "$run/commit.txt"
{ cat /etc/os-release; uname -r; lscpu; free -h; nvidia-smi; } > "$run/host.txt"
vulkaninfo --summary > "$run/vulkan.txt" 2>&1
export LD_LIBRARY_PATH="$ort/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
python3 -m venv "$run/venv"
py="$run/venv/bin/python"
"$py" -m pip install torch==2.7.1 --index-url https://download.pytorch.org/whl/cu128
"$py" -m pip install -r bindings/python/requirements-training.txt onnx==1.18.0
"$py" -m pip check
"$py" -m pip freeze > "$run/dependencies.txt"

cmake -S . -B "$run/debug" -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "$run/debug" --parallel 8
ctest --test-dir "$run/debug" --output-on-failure | tee "$run/debug-ctest.log"

build="$run/release"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DRSE_BUILD_DESKTOP=ON -DCMAKE_PREFIX_PATH="$sdl_prefix" -DRSE_SDL_LICENSE="$sdl_license" \
  -DRSE_BUILD_PYTHON=ON -DRSE_TRAINING_TESTS=ON -DPython3_EXECUTABLE="$py" \
  -DRSE_BUILD_POLICY=ON -DRSE_ONNXRUNTIME_ROOT="$ort"
cmake --build "$build" --parallel 8
ctest --test-dir "$build" --output-on-failure | tee "$run/release-ctest.log"
cmake --build "$build" --target package
"$build/rse-gameplay-tests" "$run/fixture"
rom="$run/fixture/signal-run-v1.nes"
bridge="$build/librse_python_bridge.so"
sha256sum "$run/fixture/"* > "$run/fixture-sha256.txt"
```

## Fixed-budget experiment and trained model

Run the pinned [Signal Run protocol](AUTONOMOUS_GAMEPLAY.md) on CUDA. Keep
all nine reward curves and held-out evaluations, including runs that miss
completion. Verify the M17 thresholds and confirm the GPU and budgets in the
manifest and summary. Record all policies' wall time, steps and completion,
and compare their episode rewards at matched learner-step budgets.

```bash
"$py" -B bindings/python/rse_gameplay.py --bridge "$bridge" --rom "$rom" \
  --out "$run/experiment" --device cuda --build-sha "$(cat "$run/commit.txt")" \
  2>&1 | tee "$run/experiment.log"
```

Use an archived trained M17 memory checkpoint with its recorded digest
where available. Otherwise select a passing memory checkpoint from the
rerun and use the digest in the matching experiment JSON. Record which
source was used. Set `checkpoint` and `digest` before running the export.

```bash
bundle="$run/signal-run.rsep"
"$py" -B bindings/python/rse_export_policy.py --checkpoint "$checkpoint" \
  --checkpoint-sha256 "$digest" --rom "$rom" --out "$bundle" | tee "$run/export.log"
for provider in cpu cuda; do
  "$build/rse-policy" "$rom" "$bundle" "$provider" "$run/headless-$provider.resl" \
    | tee "$run/policy-$provider.log"
done
cmp "$run/headless-cpu.resl" "$run/headless-cuda.resl"
"$build/rse-desktop" "$rom" --model "$bundle" --provider cuda \
  --replay "$run/desktop.resl" --seconds 8 | tee "$run/policy-desktop.log"
"$build/rse-policy" --replay "$rom" "$run/desktop.resl" | tee "$run/policy-replay.log"
```

Require explicit CUDA provider selection, successful parity checks and level
completion. Compare the desktop state hash to the headless playback of
`desktop.resl`, which advances to the same final tick. Repeat desktop policy
playback with a controller press before completion and verify manual takeover
persists across releases until reset. If the window is still acquiring focus
at launch, press F5 to reset and send the controller input immediately afterward.

## Desktop contention and package endurance

Use the native desktop session and the same display and audio settings for
both ten-second runs. The native CUDA comparison uses `SDL_RENDER_VSYNC=0`
for both runs to avoid the measured Vulkan presentation stalls. Use the
default VSync setting for package endurance. Confirm the Vulkan device is
the RTX 5080. Capture GPU
utilization and memory use while playback and training overlap, for example
with a separate `nvidia-smi dmon` log.

```bash
SDL_RENDER_VSYNC=0 "$build/rse-desktop" "$rom" --workspace --seconds 10 | tee "$run/baseline.log"
SDL_RENDER_VSYNC=0 "$build/rse-desktop" "$rom" --workspace --seconds 10 \
  --trainer-autostart --trainer-python "$py" \
  --trainer-script "$PWD/bindings/python/rse_train.py" --trainer-bridge "$bridge" \
  --trainer-out "$run/trainer" --trainer-profile "$PWD/bindings/python/profiles/signal-run-desktop.json" \
  --trainer-device cuda --trainer-steps 16384 | tee "$run/training.log"
```

Check the [workspace limits](AI_WORKSPACE.md#acceptance-checks), including
progress, preview updates, simulated time, starvation and maximum UI gap.
Verify the CUDA trainer manifest identifies the RTX 5080, exit saves a
checkpoint at a rollout boundary, and F9 resumes from that checkpoint. In a
fresh desktop window with the same trainer configuration, select the saved
checkpoint with F4 before pressing F9. Verify the resumed manifest names the
checkpoint and advances beyond its recorded step count.

Extract the Release ZIP with `unzip` or `cmake -E tar xf` to preserve SDL
symlinks and use its desktop executable for the
[600-second endurance test](M11_VALIDATION.md#acceptance-record).
Generate and verify the frozen acceptance set with
`cmake -DGENERATOR="$build/rse-acceptance-tests" -DOUTPUT="$run/acceptance-set" -P cmake/VerifyAcceptance.cmake`.
Record the package digest and preserve the complete playback report.
Keep the window focused or set `SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1`
for the scripted virtual controller.

## Parallel scaling and release decision

Run each supported row of the [batch benchmark](PARALLEL_HEADLESS.md#reproducing-the-benchmark)
at 1, 2, 4, 8, 16 and 32 sessions with 90 steps, first with one worker and
then with one worker per session. Compare canonical hashes within each row.
Record elapsed time, throughput, CPU use, peak RSS and bandwidth.

```bash
"$build/rse-acceptance-tests" --benchmark-disabled | tee "$run/disabled-observation-ms.csv"
for slots in 1 2 4 8 16 32; do
  "$build/rse-batch-tests" --benchmark "$slots" 1 90
  "$build/rse-batch-tests" --benchmark "$slots" "$slots" 90
done | tee "$run/parallel.csv"
```

Report missing rows or failed gates with their logs in the
[M20 acceptance record](AI_MVP_ACCEPTANCE.md).
