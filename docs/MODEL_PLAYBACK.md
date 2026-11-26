# Native ONNX policy playback

M18 implements a versioned bundle for the Signal Run v1 **memory** PPO policy.
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
replay. Native Ubuntu validation also needs a trained M17 checkpoint and
confirmation of the actual CUDA provider.

## Local validation

Windows MSVC 19.44 Debug and Release each passed 27 CTest suites with
ONNX Runtime 1.22.0 CPU, including the exporter, fallback, invalid-bundle
rejection, manual takeover and replay. A separate dependency-free headless
Release build passed 22/22 tests. A local CPU PPO checkpoint trained for
4,096 timesteps and exported successfully. Native headless inference
finished Signal Run and reproduced canonical state hash `b2ecb10d2f415d1e`.

A six-second Windows real-time run with Vulkan and dummy audio completed
Signal Run, presented 360 frames, and exited with zero starvation requests.
Replaying its exported actions headlessly produced the same canonical
state hash `8dc95b8449cd40c2`. The automated takeover test verified that
manual input and its releases retain control until reset.
The installed Windows player rejected the system ONNX Runtime 1.17.1 with a
version error and completed successfully with the matching 1.22.0 DLL beside
the executable.

Ubuntu 24.04 WSL passed 22/22 headless Debug and 24/24 desktop Release
CTest suites. With the same authored bundle, the Linux CPU player produced
a byte-identical replay log to Windows.

The native RTX 5080 run verified CUDA inference and replay using the archived
trained M17 seed 101 checkpoint. See the [M20 acceptance record](AI_MVP_ACCEPTANCE.md#native-target-workstation).
