# AI MVP acceptance (M20)

The [roadmap](AI_MVP_ROADMAP.md) defines milestone
requirements. [M8 acceptance](MVP_ACCEPTANCE.md) remains immutable historical evidence.
[M11 validation](M11_VALIDATION.md) records the accepted session and native RTX
5080 playback baseline, including the failed virtual-host endurance rerun.
Use the [native Ubuntu handoff](M20_UBUNTU_HANDOFF.md) to collect the remaining
workstation evidence in one session.

- [ ] All existing frozen fixtures and reference suites pass; Debug/Release,
  headless without AI dependencies, desktop on Windows/Linux, sanitizer and
  packaging checks. Repeat physical playback endurance if playback path changes.
- [x] Non-mutating cycle/event inspection with defined phases, bounded streams,
  explicit overflow and equivalent observed and unobserved runs.
  See [inspection](INSPECTION.md) and the automation suite.
- [x] Full snapshot restoration and timestamped action replay match canonical
  state, events, video and PCM, including mid-DMA/instruction.
  See [state and replay](STATE_REPLAY.md) and the snapshot and session suites.
- [x] Generic API exercised by fake and NES sessions. Parallel independent sessions
  produce serial-equivalent results. See [batch scaling](PARALLEL_HEADLESS.md)
  and the Windows rerun below. Repeat on the native host.
- [x] Native Ubuntu 24.04 RTX 5080 training verified on actual CUDA device;
  checkpoint/resume, configurable observations/actions/rewards and headless CLI.
  See [M16 validation](TRAINING_SDK.md#native-ubuntu-and-rtx-5080).
- [x] Project-owned NROM game autonomous level completion meets predeclared
  evaluation threshold (80/100 held-out evaluation episodes for each M17 memory seed),
  exceeds random baseline, reports scripted baseline and variation across at
  least three training seeds. See [M17 held-out evaluation](AUTONOMOUS_GAMEPLAY.md#held-out-evaluation).
- [ ] Compare memory-only, screen-only and hybrid experiments with fixed budgets;
  success of all variants is not required. Record time/steps and reward curves.
  M17 recorded fixed-budget results, elapsed times, and final evaluation rewards.
  The M20 runner adds one episode reward curve per policy and seed.
  Native fixed-budget curve capture remains pending.
- [x] Automated testing finds an intentionally seeded defect and exports a
  deterministic replay; report coverage and limits, no universal bug claim.
  See [M17 failure reproduction](AUTONOMOUS_GAMEPLAY.md#automated-failure-reproduction).
- [ ] Export/load model in optional C++ runtime, verify inference parity within
  declared tolerances, autonomous real-time playback and manual takeover.
  [M18 CPU validation](MODEL_PLAYBACK.md#local-validation) passed.
  Native CUDA parity with the trained checkpoint remains pending.
- [ ] Desktop training configuration, preview, metrics, checkpoint and recovery;
  responsive UI and audio/video when training active, with measured limits.
  [M19 CPU acceptance](AI_WORKSPACE.md#cpu-acceptance-record) passed.
  Native CUDA contention remains pending.
- [ ] Install instructions, dependency pins/licenses, SDK/task examples,
  model provenance and reference experiment reproducible from documented steps.

## Release workflows and provenance

- Follow the [build instructions](../README.md#build-and-test) for a
  dependency-free headless build or an SDL3/Vulkan desktop build. Extract a
  desktop package before launching it. See [desktop controls](DESKTOP.md)
  and [AI workspace](AI_WORKSPACE.md) for the trainer workflow.
- Follow the [training SDK guide](TRAINING_SDK.md) for the headless CLI,
  Gymnasium environment, and JSON task authoring. The
  [workspace profile](../bindings/python/profiles/signal-run-desktop.json)
  is a runnable example. The optional Python environment uses
  [pinned direct requirements](../bindings/python/requirements-training.txt).
- Reproduce the project-owned Signal Run experiment with the
  [fixed-budget protocol](AUTONOMOUS_GAMEPLAY.md) and
  [native handoff](M20_UBUNTU_HANDOFF.md). Archive the generated fixture
  hashes, experiment manifest, episode evaluations and
  `<mode>-<seed>-rewards.jsonl` curves. Each curve contains cumulative
  episode reward at the learner step that ended the episode. It excludes
  the unfinished episode at the end of training.
- Export a trusted memory-policy checkpoint using
  [model playback](MODEL_PLAYBACK.md). Keep the checkpoint digest,
  source seed, experiment manifest and export metadata alongside the bundle.
- The desktop ZIP ships the project Apache-2.0 license and the SDL3 license
  in `licenses/SDL3-LICENSE.txt`. Vulkan drivers, ONNX Runtime and its CUDA
  dependencies, and the optional Python packages are installed separately.
  Keep their upstream notices with those installations.

## Evidence record

### Development host

The M20 branch is based on merged M19 commit `88e3419`. Windows desktop
Debug and Release passed 28/28 suites each with CPU training and ONNX Runtime
enabled. Ubuntu 24.04 WSL passed 25/25 desktop Release suites with the Python
bridge and CPU ONNX Runtime. Dependency-free WSL Debug with GCC 13.3 ASan and
UBSan passed 22/22 suites, including leak detection.
Extracted Windows and WSL Release ZIPs each passed the five-second frozen
AV input smoke with Vulkan and dummy audio. Windows presented 291 frames
and WSL presented 304 with software Vulkan. Both recorded four input
responses, three expected load errors and zero audio starvation requests.
The policy-enabled packages used an external ONNX Runtime 1.22.0.

The first full-budget CPU curve, memory seed 101, captured 1,877 completed
episodes across 16,384 learner steps. Training and evaluation took 194.3 s
and finished 100/100 held-out episodes with mean reward 1.84. Short CPU
regressions also verify curve output for screen and hybrid observations.

Across seven paired 240-frame runs, median time was 945.3 ms with no
subscription and 942.3 ms after disabling observation. Both variants use
the same idle path, and the comparison excludes subscription setup.

The Windows Release batch benchmark used 90 steps per slot on a Ryzen AI
Max+ 395. Each row produced the same canonical hash with a single worker
and with one worker per session. Bandwidth counts the copied indexed frames
and logical RAM observations. Both runs at one session used a single worker.

| Sessions | Serial frames/s | Parallel frames/s | Parallel CPU % | Peak MiB | Obs. MiB/s |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 293 | 299 | 98 | 5.4 | 35 |
| 2 | 290 | 485 | 173 | 6.0 | 57 |
| 4 | 297 | 882 | 371 | 6.9 | 104 |
| 8 | 293 | 1,312 | 615 | 9.1 | 154 |
| 16 | 281 | 1,819 | 1,281 | 12.8 | 214 |
| 32 | 282 | 2,371 | 1,944 | 20.5 | 279 |

### Native target workstation

Pending. Use the handoff to record the fixed-budget reward curves, CUDA
policy parity, desktop contention and package endurance.

Record commit/build, OS/kernel, CPU/RAM, GPU/VRAM, driver, CUDA, PyTorch,
Gymnasium, trainer and ONNX Runtime versions, seeds, ROM/task/model hashes,
evaluation starts and action repeat, training wall time and simulated steps,
reward/completion, peak memory, CPU/GPU utilization, observation bandwidth,
inference latency, and parallel throughput. Measure disabled observation overhead
and active desktop pacing against same-host baselines; establish acceptable
variation before optimization. Preserve raw logs and identify manual approvals.
No fixed speedup, GPU bitwise training determinism, full NES compatibility,
physical hardware equivalence or arbitrary application competence is promised.
