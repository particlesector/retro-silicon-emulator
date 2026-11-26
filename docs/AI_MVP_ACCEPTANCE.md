# AI MVP acceptance (M20)

The [roadmap](AI_MVP_ROADMAP.md) defines milestone
requirements. [M8 acceptance](MVP_ACCEPTANCE.md) remains immutable historical evidence.
[M11 validation](M11_VALIDATION.md) records the accepted session and native RTX
5080 playback baseline, including the failed virtual-host endurance rerun.
Use the [native Ubuntu handoff](M20_UBUNTU_HANDOFF.md) to reproduce the
workstation evidence.

- [x] All existing frozen fixtures and reference suites pass; Debug/Release,
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
- [x] Compare memory-only, screen-only and hybrid experiments with fixed budgets;
  success of all variants is not required. Record time/steps and reward curves.
  M17 recorded fixed-budget results, elapsed times, and final evaluation rewards.
  The M20 runner adds one episode reward curve per policy and seed.
  The native rerun captured all nine curves and held-out evaluations.
- [x] Automated testing finds an intentionally seeded defect and exports a
  deterministic replay; report coverage and limits, no universal bug claim.
  See [M17 failure reproduction](AUTONOMOUS_GAMEPLAY.md#automated-failure-reproduction).
- [x] Export/load model in optional C++ runtime, verify inference parity within
  declared tolerances, autonomous real-time playback and manual takeover.
  [M18 CPU validation](MODEL_PLAYBACK.md#local-validation) passed.
  Native CUDA parity with the trained checkpoint passed below.
- [x] Desktop training configuration, preview, metrics, checkpoint and recovery;
  responsive UI and audio/video when training active, with measured limits.
  [M19 CPU acceptance](AI_WORKSPACE.md#cpu-acceptance-record) passed.
  Native CUDA contention and checkpoint resume passed below.
- [x] Install instructions, dependency pins/licenses, SDK/task examples,
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

Tested base commit `1f0e2ce794c443c31e7057d615263597894c3ef7` on Ubuntu
24.04.5 LTS, kernel 7.0.0-42-generic, GCC 13.3, Ryzen 9 7900X (12 cores),
61 GiB of usable RAM, and a GeForce RTX 5080 (16 GiB, driver 595.99.02).
The desktop used SDL 3.4.14, X11, Vulkan and PulseAudio. Python 3.12.3 used
PyTorch 2.7.1+cu128, CUDA 12.8, cuDNN 9.7.1.26, Gymnasium 1.2.2, SB3 2.7.1,
NumPy 2.2.6 and ONNX 1.18.0. The CUDA-enabled C++ runtime came from Microsoft's ONNX
Runtime 1.22.0 Linux GPU NuGet package, with CUDA and cuDNN libraries from the
PyTorch environment. The final desktop retest used the VSync configuration
change in this PR (renderer blob `138d4408013a697d5496f743d172048ff2f26293`).

#### Build and package validation

Native headless Debug passed 22/22 suites, native headless Debug with
ASan/UBSan and leak detection passed 22/22, and desktop Release with policy,
Python bridge and training tests passed 28/28. The frozen acceptance set
passed its digest checks. The renderer also passed Vulkan readback and
VSync mode tests on Xvfb/lavapipe. The final Release ZIP resolved SDL from
its bundled `lib/` directory and has SHA-256
`07cd7d5a6fdadd5e60f2309aae1d3c90a1ccb4488ee0fbe3190f7f7034094ae5`.

The extracted final package exited successfully after 600,003 ms on Vulkan
and PulseAudio with VSync enabled. It presented 36,045 frames, advanced
599,783 simulated ms, and
reported five verified Vulkan readbacks, two loads, one reset, three
expected load errors, and zero audio starvation requests or bytes. Four
input responses took 28,106–35,465 µs (mean 32,023 µs). Peak buffering was
1,824 audio bytes and one video frame. The maximum UI loop gap was 152.3 ms
for the endurance workload, which includes a scripted GUI stall.

The first endurance run of the rebuilt package reached 600,000 ms and
passed the video and input checks, but failed the zero-starvation gate.
It recorded one 126-byte request at SDL timestamp 141,415 ms. The same
package and settings passed on the isolated retry. A 12-second output-sink
sample during the first run had a dominant 440 Hz tone and zero silent
10 ms windows in each analyzed interior second. Reports are retained in
`final-endurance.log` and `final-endurance-retry.log`.

#### Fixed-budget CUDA experiment

Each policy used 16,384 learner steps. Evaluation cycled through starts
1, 3, 5, 7 and 9 with episode seeds 1000–1099 and a 20-step horizon.
Each action covered two PPU frames. The protocol SHA-256 is
`f1bad1242e07d07ae559376633fd652a130d389950ca3c5249f092bbf730ed98`
and both Release configurations produced the same Python bridge SHA-256,
`6efb93521f61dd7aaaf0256692ca3d318f79195edc6ec243993a2957e91f1845`.

| Mode | Seed | Learner steps | Wall s | Completed / 100 | Mean held-out reward | Recorded training episodes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Memory | 101 | 16,384 | 162.8 | 100 | 1.840 | 1,878 |
| Memory | 202 | 16,384 | 165.1 | 100 | 1.840 | 1,944 |
| Memory | 303 | 16,384 | 165.7 | 100 | 1.840 | 1,935 |
| Screen | 101 | 16,384 | 176.7 | 100 | 1.840 | 1,953 |
| Screen | 202 | 16,384 | 179.3 | 100 | 1.840 | 2,007 |
| Screen | 303 | 16,384 | 178.9 | 100 | 1.840 | 2,004 |
| Hybrid | 101 | 16,384 | 175.1 | 100 | 1.840 | 1,967 |
| Hybrid | 202 | 16,384 | 180.9 | 100 | 1.840 | 2,018 |
| Hybrid | 303 | 16,384 | 179.6 | 100 | 1.840 | 2,016 |

The random baseline completed 54/100 episodes (mean reward 0.624).
The scripted baseline completed 100/100 (1.840). Each memory seed exceeded
the required 80/100 and the random baseline. Wall time includes training,
checkpoint save and evaluation. Other validation workloads overlapped the
early policies, so these times describe the measured session.

The table below averages each seed's mean episode reward, giving equal
weight to seeds. An episode belongs to the learner-step window in which it
finished.

| Mode | Steps 1–4,096 | 4,097–8,192 | 8,193–12,288 | 12,289–16,384 |
| --- | ---: | ---: | ---: | ---: |
| Memory | 0.943 | 1.921 | 1.966 | 1.992 |
| Screen | 1.182 | 1.944 | 1.969 | 1.993 |
| Hybrid | 1.198 | 1.959 | 1.968 | 1.994 |

Screen and hybrid had higher training episode rewards in the first window.
All three modes reached almost the same training reward in the last window.
The 100 held-out trials repeat five deterministic start positions.
A 20-sample device-wide GPU trace taken every five seconds during the
experiment reached 5% sampled SM utilization and 1,014 MiB of framebuffer
use. The experiment process's sampled peak RSS was 2.31 GiB.

#### CUDA policy playback

The archived M17 CUDA memory seed 101 checkpoint previously finished 100/100
held-out episodes. Its SHA-256 is
`3400654378ce0f5cec5b0abb8b0f474b060c0b49fffff151cefdde52fe57a1d1`.
The generated Signal Run ROM has SHA-256
`bc479da4a7acadde14447a308820780992c8ac756cc04daf665c6772d2aa5517`.
The exported ONNX payload has SHA-256
`2a98c134a4ce7a904c0008800bf20f06bdf4c3e971dac1e5faea916bc6188adf`,
and the policy bundle has SHA-256
`a930e66e164970398246de15056ff79c4e2aab26c7fc267bca98aedd9631aeae`.
CPU and explicitly selected CUDA passed the bundle's 12-input logits and
action-agreement checks. Both completed at tick 8,220,000 with byte-identical
headless replays and state hash `b2ecb10d2f415d1e`. The eight-second desktop
CUDA run completed with zero audio starvation and matched the headless
playback of its recording at tick 169,805,068 (hash `f8dff66ad8ba16b7`).
For 12,000 warmed `Model::choose` calls over all 12 positions, median/p95
latencies were 2.01/2.05 µs on CPU and 28.76/30.94 µs on CUDA. This timing
includes synchronous inference and selection, with model loading excluded.
A controller-mapped X11 key press immediately after reset prevented
autonomous completion and retained manual control after release. A second
reset restored policy control and autonomous completion.

#### Desktop CUDA training and resume

Two isolated runs at the default VSync setting measured maximum training UI
gaps of 100.056 ms and 100.633 ms, exceeding the 100 ms gate. A profiled
repeat measured 98.4 ms inside `SDL_RenderPresent` during the longest draw.
The desktop now honors `SDL_RENDER_VSYNC`, with VSync enabled by default.
The validated CUDA workspace configuration sets `SDL_RENDER_VSYNC=0` for
both baseline and training. Disabling refresh synchronization can cause
tearing. Both runs used the RTX 5080 Vulkan device on X11 and the same
PulseAudio sink, with zero audio starvation.

| Ten-second run | Frames | Simulated ms | Max UI gap (ms) | Trainer steps observed | Preview updates |
| --- | ---: | ---: | ---: | ---: | ---: |
| Baseline | 592 | 9,863 | 15.6 | — | — |
| CUDA trainer | 601 | 10,013 | 28.6 | 1,142 | 31 |

One-second device-wide NVIDIA samples peaked at 658 MiB of memory in the
baseline and 1,020 MiB with the workspace trainer. Peak sampled SM
utilization was 23% and 12%, respectively.
The trainer reported 17.5 MiB of peak CUDA tensor allocation. Exit saved
the final PPO checkpoint at step 1,216, a multiple of the 64-step rollout.
On an isolated X11 display on the same workstation, F4 selected that
checkpoint and F9 resumed RTX 5080 CUDA training through step 4,096.
The resumed manifest records the exact source path. The workspace profile SHA-256 is
`5f71c97e7edadc042bd728f3252bbeecfd90b325a4fb742d7bcaa9772817ae24`.
The saved step-1,216 checkpoint SHA-256 is
`670de6bedfe500a1c6a484a2a4517646f994c63c4d3f46441b031e3021a181a2`.

#### Parallel scaling and disabled observation

The Release batch benchmark used 90 steps per session. Each pair matched
its aggregate canonical state hash. Both one-session runs used one worker.
CPU percentages count 100% per logical core. Peak RSS and logical bandwidth
are from the one-worker-per-session run. The raw CSV includes both variants.

| Sessions | Serial s | Parallel s | Serial frames/s | Parallel frames/s | Parallel CPU % | Peak MiB | Obs. MiB/s | Canonical hash |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 1 | 0.246 | 0.249 | 366 | 361 | 100 | 3.9 | 42.4 | `73aa1528430a1553` |
| 2 | 0.507 | 0.251 | 355 | 717 | 196 | 4.2 | 84.2 | `c4bd4675a72257eb` |
| 4 | 0.979 | 0.363 | 368 | 993 | 344 | 4.7 | 116.6 | `cb29c51fa850d46a` |
| 8 | 2.016 | 0.555 | 357 | 1,298 | 528 | 5.8 | 152.4 | `5073a23008e6c23a` |
| 16 | 3.977 | 0.572 | 362 | 2,517 | 1,078 | 9.2 | 295.7 | `b914720c6bc8444a` |
| 32 | 7.884 | 0.895 | 365 | 3,219 | 1,703 | 16.7 | 378.2 | `4f9bef72874e985f` |

The 32-session pair measured 8.8× serial throughput. In seven paired
240-frame runs, the no-subscription and disabled-observation medians were
696.4 ms and 694.9 ms, with a median paired difference of −0.11 ms.
Both cases use the same idle path and the timed region excludes
subscription setup.

Raw native logs are under `build/m20-ubuntu.z0LOWE/`, including all nine
reward curves, episode evaluations, manifests and checkpoint digests in
`experiment/`.

No fixed speedup, GPU bitwise training determinism, full NES compatibility,
physical hardware equivalence or arbitrary application competence is promised.
