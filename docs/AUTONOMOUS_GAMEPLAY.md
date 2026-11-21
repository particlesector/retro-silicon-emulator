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

## Native Ubuntu and RTX 5080 validation

The native handoff tested source revision
`9fc2abc27458569540d42880dfa4025a10e2a0da` on Ubuntu 24.04.5 LTS,
kernel `7.0.0-42-generic`, with a Ryzen 9 7900X and 61 GiB of usable RAM.
The RTX 5080 reported 16,303 MiB of VRAM and driver `595.99.02`.
The SDK used Python 3.12.3, PyTorch 2.7.1+cu128, CUDA runtime 12.8,
Gymnasium 1.2.2, Stable-Baselines3 2.7.1, and NumPy 2.2.6.
The workstation's system Python lacked `ensurepip` during M16 setup, so
this run created the workspace venv with `--without-pip` and bootstrapped
pip inside it before installing the pinned dependencies.

Debug passed 22/22 CTest suites. Release with the Python bridge and training
tests passed 25/25. The fixture's seeded obstacle failure and replay passed,
and the generated replay matched the SHA-256 above.

| Input | SHA-256 |
| --- | --- |
| Pinned protocol | `f1bad1242e07d07ae559376633fd652a130d389950ca3c5249f092bbf730ed98` |
| Healthy ROM | `bc479da4a7acadde14447a308820780992c8ac756cc04daf665c6772d2aa5517` |
| Defective ROM | `925f48360414965712f149a48d33b360e5e1526097f0cd1a225c332fc049a260` |

The local evidence directory is `build/m17-ubuntu.sjsILs/`. It contains the
host report, dependency freeze, test logs, generated ROMs, failure replay,
and experiment output.

### Held-out evaluation

All nine policies trained on CUDA for 16,384 timesteps each. Every memory
seed cleared the 80/100 threshold, and all policies beat random and matched
the scripted baseline. All 100 evaluations per policy completed across the
five held-out starts. The recorded digests matched all nine model files.

| Policy | Seed | Completions | Mean reward | Train and evaluate (s) | Model SHA-256 |
| --- | ---: | ---: | ---: | ---: | --- |
| random | — | 54/100 | 0.624 | — | — |
| scripted | — | 100/100 | 1.840 | — | — |
| memory | 101 | 100/100 | 1.840 | 158.3 | `3400654378ce0f5cec5b0abb8b0f474b060c0b49fffff151cefdde52fe57a1d1` |
| memory | 202 | 100/100 | 1.840 | 161.3 | `2d5c077c5bb1121177daca5beecd27d89da66cd17836b6e9a35d7fb19c79f4e1` |
| memory | 303 | 100/100 | 1.840 | 161.0 | `b42f7ea1bbe7e86562f3ae1abc73a0375817a232095fe66098a9f67325e32dde` |
| screen | 101 | 100/100 | 1.840 | 173.9 | `97a64ed115326e6284db13ba694e2d881e8bad874ec986ae827ed9019e7ed6dd` |
| screen | 202 | 100/100 | 1.840 | 175.6 | `5032b6c97fb8e0ddfce446f418e479a7c3dfece1031a3d27e911e8a85880f712` |
| screen | 303 | 100/100 | 1.840 | 175.1 | `f5c2999e6b2f3d84d72d48c82f13474d256b0cf9a0ff0cedd4f09b131ceae2d9` |
| hybrid | 101 | 100/100 | 1.840 | 173.1 | `26f2e366fdc2c276f1681527485b7d12806afc2ae3b71d83657d984cd0c32ee1` |
| hybrid | 202 | 100/100 | 1.840 | 174.2 | `5c7a4f0de4221e7facc13e59dba2f56f630bf899c64bf061e601a381cfdf9d62` |
| hybrid | 303 | 100/100 | 1.840 | 176.3 | `6728d3bc27008728da6a25d94769dc8ae16db383d98345b279eeecdeb7cb29a9` |
