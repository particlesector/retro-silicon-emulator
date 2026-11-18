# AI MVP acceptance (M20)

Proposed gates, all pending. [Roadmap](AI_MVP_ROADMAP.md) defines milestone
requirements. [M8 acceptance](MVP_ACCEPTANCE.md) remains immutable historical evidence.
[M11 validation](M11_VALIDATION.md) records the accepted session and native RTX
5080 playback baseline, including the failed virtual-host endurance rerun.

- [ ] All existing frozen fixtures and reference suites pass; Debug/Release,
  headless without AI dependencies, desktop on Windows/Linux, sanitizer and
  packaging checks. Repeat physical playback endurance if playback path changes.
- [ ] Non-mutating cycle/event inspection with defined phases, bounded streams,
  explicit overflow; observed and unobserved logical runs match.
- [ ] Full snapshot restoration and timestamped action replay match canonical
  state, events, video and PCM, including mid-DMA/instruction.
- [ ] Generic API exercised by fake and NES sessions; parallel independent sessions
  produce serial-equivalent results. Record 1/2/4/8/16/32 scaling where feasible.
- [ ] Native Ubuntu 24.04 RTX 5080 training verified on actual CUDA device;
  checkpoint/resume, configurable observations/actions/rewards and headless CLI.
- [ ] Project-owned NROM game autonomous level completion meets predeclared
  evaluation threshold (M17 proposed 80/100 held-out evaluation episodes),
  exceeds random baseline, reports scripted baseline and variation across at
  least three training seeds. Report failures rather than cherry-pick results.
- [ ] Compare memory-only, screen-only and hybrid experiments with fixed budgets;
  success of all variants is not required. Record time/steps and reward curves.
- [ ] Automated testing finds an intentionally seeded defect and exports a
  deterministic replay; report coverage and limits, no universal bug claim.
- [ ] Export/load model in optional C++ runtime, verify inference parity within
  declared tolerances, autonomous real-time playback and manual takeover.
- [ ] Desktop training configuration, preview, metrics, checkpoint and recovery;
  responsive UI and audio/video when training active, with measured limits.
- [ ] Install instructions, dependency pins/licenses, SDK/task examples,
  model provenance and reference experiment reproducible from documented steps.

## Evidence record (fill during implementation)

Record commit/build, OS/kernel, CPU/RAM, GPU/VRAM, driver, CUDA, PyTorch,
Gymnasium, trainer and ONNX Runtime versions, seeds, ROM/task/model hashes,
evaluation starts and action repeat, training wall time and simulated steps,
reward/completion, peak memory, CPU/GPU utilization, observation bandwidth,
inference latency, and parallel throughput. Measure disabled observation overhead
and active desktop pacing against same-host baselines; establish acceptable
variation before optimization. Preserve raw logs and identify manual approvals.
No fixed speedup, GPU bitwise training determinism, full NES compatibility,
physical hardware equivalence or arbitrary application competence is promised.
