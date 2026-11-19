# AI training SDK

The optional Python SDK provides `rse_training.EmulatorEnv` and a PPO,
random, or scripted runner. The SDK uses the in-process C++ automation
bridge. It supports one Gymnasium environment per adapter. The M15
C++ batch API remains available to native clients.

For milestone acceptance on the RTX 5080 workstation, use the
[native Ubuntu handoff](M16_UBUNTU_HANDOFF.md).

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

## Validation

Windows MSVC 19.44 Debug and Release each passed 24 CTest suites with
desktop, the bridge, and the optional training smoke enabled. The SDK tests
exercise Gymnasium's environment checker, deterministic replay across all
three observation modes, malformed profiles, termination and truncation,
PPO checkpoint and resume, and both baselines.

A separate Windows Release run used Python 3.13.3, PyTorch 2.7.1 CPU,
Gymnasium 1.2.2 and Stable-Baselines3 2.7.1. It completed 128 PPO timesteps
at seed 19 and saved a checkpoint, run manifest and metrics under
`build/m16/cpu-ppo-smoke`.

### Native Ubuntu and RTX 5080

The Ubuntu handoff passed on 2026-11-18 at
`1afd23274918c17f84994f55de2b046b102d4823`.

| Item | Result |
| --- | --- |
| Host | Ubuntu 24.04.5 LTS, kernel 7.0.0-42-generic, Ryzen 9 7900X, 61 GiB RAM reported by `free` |
| GPU | GeForce RTX 5080, 16,303 MiB VRAM reported by `nvidia-smi`, driver 595.99.02, compute capability 12.0 |
| Build tools | GCC 13.3.0, CMake 3.28.3, Ninja 1.11.1 |
| SDK | Python 3.12.3, PyTorch 2.7.1+cu128, CUDA runtime 12.8, Gymnasium 1.2.2, Stable-Baselines3 2.7.1, NumPy 2.2.6 |
| CTest | Debug 21/21, Release with bridge and training tests 23/23 |
| PPO smoke | Model device `cuda`, seed 19, 128 timesteps, 18,301,440 bytes peak CUDA allocation, 1.018 s elapsed |
| Checkpoint SHA-256 | `bc21a647ab9c7c9832495c3e956da4ad0b7eda9646d692b2916f5d7520af504a` |

The handoff verified the manifest, PPO metrics, and checkpoint digest.
Logs, the dependency freeze, the manifest, metrics, and generated artifacts
are in the local `build/m16-ubuntu.L4xb5o/` directory.

The initial `python3 -m venv` command failed because the system Python
lacked `ensurepip`. The retry created the workspace environment with
`--without-pip` and bootstrapped pip inside it before installing the
pinned packages. The initial error and host details are in
`build/m16-ubuntu.EG2F3l/`.
