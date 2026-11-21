# M17 handoff for native Ubuntu and RTX 5080

Run this from a clean checkout on the native Ubuntu 24.04 workstation.
Use a separate worktree if the current checkout contains local work.

```bash
git fetch origin feature/m17-autonomous-gameplay
git switch --track origin/feature/m17-autonomous-gameplay
```

If the tracking branch already exists locally, switch to it and fast-forward
from `origin/feature/m17-autonomous-gameplay`.

Run the following from the repository root. Use native Ubuntu with a working
NVIDIA driver, CMake, Ninja, a C++23 compiler and Python with `venv`.
On Ubuntu, install `python3-venv` if virtual environment creation reports
missing `ensurepip`. If multiple GPUs are available, select the RTX 5080
with `CUDA_VISIBLE_DEVICES` before invoking the experiment.

```bash
set -euo pipefail
mkdir -p build
run=$(mktemp -d "$PWD/build/m17-ubuntu.XXXXXX")
printf 'Evidence directory: %s\n' "$run"
git rev-parse HEAD | tee "$run/commit.txt"
{ cat /etc/os-release; uname -r; lscpu; free -h; nvidia-smi; } | tee "$run/host.txt"
python3 -m venv "$run/venv"
py="$run/venv/bin/python"
"$py" -m pip install torch==2.7.1 --index-url https://download.pytorch.org/whl/cu128
"$py" -m pip install -r bindings/python/requirements-training.txt
"$py" -m pip check
"$py" -m pip freeze > "$run/dependencies.txt"

cmake -S . -B "$run/debug" -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "$run/debug" --parallel 8
ctest --test-dir "$run/debug" --output-on-failure 2>&1 | tee "$run/debug-ctest.log"

cmake -S . -B "$run/release" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DRSE_BUILD_PYTHON=ON -DRSE_TRAINING_TESTS=ON -DPython3_EXECUTABLE="$py"
cmake --build "$run/release" --parallel 8
ctest --test-dir "$run/release" --output-on-failure 2>&1 | tee "$run/release-ctest.log"
"$run/release/rse-gameplay-tests" "$run/signal-run"
sha256sum "$run/signal-run/"* | tee "$run/fixture-sha256.txt"

# The runner writes a result for each of the nine policies. Preserve the
# output directory even if the command returns a nonzero exit status.
"$py" -B bindings/python/rse_gameplay.py \
  --bridge "$run/release/librse_python_bridge.so" \
  --rom "$run/signal-run/signal-run-v1.nes" \
  --out "$run/m17-rtx5080" --device cuda --build-sha "$(git rev-parse HEAD)" \
  2>&1 | tee "$run/gameplay.log"
```

## Acceptance and evidence

- Keep the ROM, seeds, training budget, held-out starts, and threshold fixed
  as documented in the [Signal Run protocol](AUTONOMOUS_GAMEPLAY.md).
- Verify successful Debug and Release CTest, including the fixture's seeded
  obstacle failure and replay. The healthy ROM hash must match the pinned
  profile and the replay hash must match the protocol document.
- Verify the experiment manifest names the RTX 5080 and reports CUDA 12.8.
  Verify every policy used CUDA and trained for 16,384 timesteps. Compare
  all nine completion counts and mean rewards with both baselines. Each
  memory-policy seed must complete at least 80/100 and beat random. Keep
  the screen and hybrid results, including any missed completions.
- Verify each model's SHA-256 against its JSON record. Record the tested SHA,
  CTest totals, GPU and driver, dependency versions, protocol and ROM hashes,
  both baseline results, and each policy's seed, completion count, mean
  reward, and model hash in `docs/AUTONOMOUS_GAMEPLAY.md`.
- Keep logs, manifests, episode records, generated ROMs, models, and replay
  files under the evidence directory. Commit the documentation and use the
  existing branch for the handoff back. Update the roadmap and mark M17
  complete only after the checks pass. If the run fails, preserve the results
  and report the failure and the affected modes or seeds.
