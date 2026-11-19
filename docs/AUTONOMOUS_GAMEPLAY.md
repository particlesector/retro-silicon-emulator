# M17 autonomous gameplay and automated testing

Signal Run is a project-owned Apache-2.0 NROM/CHR-RAM fixture. It uses a
single controller, a visible track, obstacles at cells 3 and 7, and a goal
at cell 11. Right advances across clear ground. Right+A jumps across an
obstacle. Choosing the wrong movement records a collision and leaves the
player in place. Each successful move adds one point. Finishing latches
the goal flag. The generator keeps the frozen M8 fixtures intact.

## Pinned evaluation protocol

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

## Running on the RTX 5080 workstation

Use the [standalone Ubuntu handoff](M17_UBUNTU_HANDOFF.md) for checkout,
build, experiment, and evidence collection.

Use the native Ubuntu setup and pinned CUDA 12.8 environment from the
[M16 handoff](M16_UBUNTU_HANDOFF.md). Stay on the M17 source revision and
follow the M16 commands through the Release CTest run. The optional SDK
tests must be enabled in Release.
With `$run` pointing to the working directory from that setup:

```bash
"$run/release/rse-gameplay-tests" "$run/signal-run"
"$run/venv/bin/python" -B bindings/python/rse_gameplay.py \
  --bridge "$run/release/librse_python_bridge.so" \
  --rom "$run/signal-run/signal-run-v1.nes" \
  --out "$run/m17-rtx5080" --device cuda --build-sha "$(git rev-parse HEAD)"
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

## Local validation

Windows Debug and Release each passed 26 CTest suites, including the fixture,
failure replay, frozen M8 hashes, screen/RAM alignment, and the optional SDK
checks. A short CPU PPO wiring test exercised each observation mode.
The pinned baselines completed 54/100 episodes for random (mean reward 0.624)
and 100/100 for scripted (mean reward 1.840). Episode records are under
`build/m17/windows-baselines-v1/`.

Native Ubuntu and the nine-policy RTX 5080 experiment remain pending. The
local WSL VM timed out before the Linux build could start.
