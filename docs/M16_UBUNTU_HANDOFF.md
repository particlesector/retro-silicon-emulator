# M16 validation on native Ubuntu with an RTX 5080

From the RTX 5080 workstation, fetch and check out
`origin/feature/m16-training-sdk`. Use a separate worktree if your existing
checkout has local changes. Run these commands from the repository root on
native Ubuntu 24.04. The headless build needs CMake, Ninja, a C++23 compiler,
Python with `venv`, and a working NVIDIA driver.

```bash
git fetch origin feature/m16-training-sdk
git switch --track origin/feature/m16-training-sdk
```

If the tracking branch already exists locally, use
`git switch feature/m16-training-sdk` and `git merge --ff-only origin/feature/m16-training-sdk`.

```bash
set -euo pipefail
mkdir -p build
run=$(mktemp -d "$PWD/build/m16-ubuntu.XXXXXX")
git rev-parse HEAD | tee "$run/commit.txt"
{ cat /etc/os-release; uname -r; lscpu; free -h; nvidia-smi; } | tee "$run/host.txt"

python3 -m venv "$run/venv"
py="$run/venv/bin/python"
"$py" -m pip install torch==2.7.1 --index-url https://download.pytorch.org/whl/cu128 2>&1 | tee "$run/torch-install.log"
"$py" -m pip install -r bindings/python/requirements-training.txt 2>&1 | tee "$run/sdk-install.log"
"$py" -m pip check
"$py" -m pip freeze > "$run/dependencies.txt"

cmake -S . -B "$run/debug" -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "$run/debug" --parallel 8
ctest --test-dir "$run/debug" --output-on-failure 2>&1 | tee "$run/debug-ctest.log"

cmake -S . -B "$run/release" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DRSE_BUILD_PYTHON=ON -DRSE_TRAINING_TESTS=ON -DPython3_EXECUTABLE="$py"
cmake --build "$run/release" --parallel 8
ctest --test-dir "$run/release" --output-on-failure 2>&1 | tee "$run/release-ctest.log"
"$run/release/rse-acceptance-tests" --write-set "$run/fixtures"

# Set CUDA_VISIBLE_DEVICES before this command if GPU 0 is not the RTX 5080.
"$py" -B bindings/python/rse_train.py \
  --bridge "$run/release/librse_python_bridge.so" \
  --rom "$run/fixtures/rse-loop-v1.nes" \
  --profile bindings/python/profiles/nrom-smoke.json \
  --out "$run/ppo-cuda" --device cuda --seed 19 \
  --steps 128 --rollout 32 --checkpoint-every 64 \
  --build-sha "$(git rev-parse HEAD)" 2>&1 | tee "$run/cuda-smoke.log"

"$py" - "$run/ppo-cuda" "$(git rev-parse HEAD)" <<'PY'
import hashlib
import json
from pathlib import Path
import sys

root = Path(sys.argv[1])
run = json.loads((root / "manifest.json").read_text())
checkpoint = json.loads((root / "checkpoint.json").read_text())
metrics = [json.loads(line) for line in (root / "metrics.jsonl").read_text().splitlines()]
assert run["status"] == "complete" and run["build_sha"] == sys.argv[2], run
assert run["device"] == "cuda" and run["model_device"].split(":")[0] == "cuda", run
assert "RTX 5080" in run["gpu_name"] and run["gpu_capability"] == [12, 0], run
assert run["cuda_runtime"] == "12.8" and run["cuda_peak_allocated_bytes"] > 0, run
assert run["seed"] == 19 and run["timesteps"] == checkpoint["timesteps"] == 128, run
assert hashlib.sha256((root / "checkpoint.zip").read_bytes()).hexdigest() == checkpoint["checkpoint_sha256"]
assert any("ppo" in record for record in metrics), metrics
print(json.dumps(run, indent=2, sort_keys=True))
PY
printf 'Evidence directory: %s\n' "$run"
```

Report the tested SHA, Ubuntu/kernel, CPU/RAM, GPU/VRAM and driver, dependency
versions, both CTest totals, PPO device, timesteps, peak CUDA allocation,
wall time, and checkpoint SHA-256. Preserve the logs, manifest and metrics
under the printed directory. Keep generated ROMs and checkpoints out of Git.

Record the outcome in [TRAINING_SDK.md](TRAINING_SDK.md). Mark M16 complete in
the roadmaps after all checks pass. On failure, record the failing command,
output and device information. CPU execution leaves the CUDA gate open.
