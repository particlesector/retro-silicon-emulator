# AI-enabled emulation MVP (M12–M20)

M12 is complete. M13 is the next milestone.
See [M11 validation](M11_VALIDATION.md) for the accepted session baseline.
Target: native Ubuntu 24.04 LTS workstation, RTX 5080 (16 GB VRAM), 64 GB RAM;
record actual CPU, driver, CUDA/PyTorch versions and build SHA during validation.
NES NTSC NROM remains the sole required real machine; a fake session proves generic
contracts. Existing Windows/Linux core and desktop support must not regress.

## Milestones and acceptance gates

### M12 — Observation and inspection
- [x] Discover typed memory domains, registers, signals and event capabilities by
  stable IDs, widths, address units and clock domains; unsupported operations explicit.
- [x] Non-mutating peek/bulk read (including PPU/APU/controller side effects);
  distinguish physical RAM, mapped addresses, mirrors and unavailable values.
- [x] Subscribe by address/range/event, master tick, CPU cycle, instruction,
  scanline or frame where supported. Specify pre/post phase and stable ordering.
  Provide deltas, bounded buffers, sequence IDs and explicit loss/overflow.
- [x] Compare frozen M8 hashes and trace ordering with observation on/off, including
  DMA and interrupts. Benchmark disabled overhead against same-build baseline.

[Inspection contract and validation](INSPECTION.md)

### M13 — Automation and external API
- [ ] Session-owner serialized reset, run-until/step with tick budget, timestamped
  named input, pause and breakpoint/watchpoint control; never mutate from callbacks.
- [ ] Optional Python bindings over native C++ session API; release GIL during
  execution, define buffer ownership and safe teardown; no Python dependency in core.
- [ ] Versioned binary observation batches, schema discovery and bounded transport.
  Start in-process; local IPC/shared memory only when measured need warrants it.
  Lossless mode blocks at safe boundaries or fails explicitly; live preview may
  drop with counters. No public network service required.
- [ ] Test fake and NES sessions, malformed requests, timing, reentrancy and replay.

### M14 — Full state and replay
- [ ] Versioned snapshots include clocks/phases, CPU microstate, PPU/APU, DMA,
  bus latches, RAM, cartridge/mapper, controller, pending events, RNG if present,
  deterministic PCM/resampler state and input timeline; exclude host resources.
- [ ] Validate ROM/config hashes, versions, bounds and integrity before transactional
  restore. Clear/reprime host queues; define output cursor semantics.
- [ ] Restore mid-instruction/DMA/frame, replay across chunk sizes and compare
  canonical logical state, events, video and PCM; test corrupted/incompatible files.
- [ ] Record portable timestamped input/action logs and reproducible failure cases.

### M15 — Parallel headless execution
- [ ] Independent sessions, no shared mutable machine state, bounded worker pool,
  deterministic per-environment seed/reset and batched observations/actions.
- [ ] Headless accelerated execution without wall-clock pacing or display/audio
  devices; suppress only host output, never required emulated hardware work.
- [ ] Benchmark 1/2/4/8/16/32 environments where feasible: frames/ticks/steps per
  second, CPU/RAM, observation bandwidth and GPU utilization once connected.
  Compare hashes with serial execution; select concurrency from measured throughput.

### M16 — AI training SDK
- [ ] Optional Gymnasium adapter with reset(seed, options), step(action),
  observation/action spaces, reward, terminated/truncated and info; frame/action
  repeat explicitly defined, integer simulated time authoritative.
- [ ] Configurable screen-only, memory-only and hybrid observations; task-specific
  reward/termination profiles separated from machine code. Validate schema and ROM.
- [ ] PyTorch/CUDA on RTX 5080 with pinned compatible dependencies; PPO baseline
  (e.g. Stable-Baselines3), random/scripted baselines, checkpoint/resume, metrics,
  seeds and run manifests. CPU fallback useful for development.
- [ ] Verify actual CUDA execution, deterministic environment replay and a small
  training smoke. Do not claim bitwise reproducible GPU learning across drivers.

### M17 — Autonomous gameplay and testing
- [ ] Build project-owned licensed NROM learning fixture with movement, obstacles,
  rewards and an unambiguous finish. Keep frozen M8 fixtures unchanged.
- [ ] Before training, pin evaluation protocol, episode budget, held-out starts
  and threshold (proposed >=80% completion over 100 evaluation episodes and
  improvement over random baseline). Record seeds, model hash, reward and results.
- [ ] Train/evaluate on RTX 5080; compare observation modes and report failures.
  Optional user-supplied compatible commercial ROM experiment; no redistribution.
- [ ] Demonstrate automated testing via coverage/novel-state exploration or
  assertions on an intentionally defective test build; capture and replay a
  real reproducible failure. AI is not an accuracy oracle.

### M18 — Native model playback
- [ ] Export supported policy to ONNX; version/hash model plus preprocessing,
  input/output shapes, action mapping and task metadata. Reject incompatible models.
- [ ] Optional C++ ONNX Runtime adapter, CPU fallback and tested CUDA provider;
  no Python runtime needed for playback. Compare inference against training tool
  on fixed inputs with declared tolerances and action-selection semantics.
- [ ] Run real-time and headless autonomous episodes; manual takeover and clean
  shutdown. In synchronous mode wait for inference, never skip emulated cycles.
  Timestamp chosen actions for exact replay regardless of model nondeterminism.

### M19 — Desktop AI workspace
- [ ] SDL3/Vulkan UI for ROM/task selection, observation configuration, start/stop,
  checkpoint/resume, model loading, live preview, reward/throughput/loss metrics,
  selected memory and controller actions. Optional trainer process, not UI thread.
- [ ] Worker lifecycle, cancellation at safe boundaries, bounded telemetry, errors,
  manual takeover and disconnected trainer recovery. CLI remains fully usable.
- [ ] Verify responsive UI and existing audio/video playback with training active;
  document resource limits or reduced concurrency if contention occurs.

### M20 — MVP release and validation
- [ ] Execute [AI acceptance](AI_MVP_ACCEPTANCE.md) on native target workstation;
  preserve existing platform regression and packaging gates.
- [ ] Document installation, CLI/desktop workflows, task authoring, SDK examples,
  model provenance, third-party licenses and reproducible reference experiment.
- [ ] Publish measured throughput and limitations, not theoretical scaling claims.

## Scope and decisions
Core stays C++23/Apache-2.0; optional Python, Gymnasium, PyTorch/CUDA and ONNX
dependencies are isolated and audited. CPU emulates NES; GPU trains/infers and
renders UI. No GPU hardware emulation, new console cores, distributed training,
visual circuit editor or universal pretrained agent required. Generalization to
unseen tasks, arbitrary ROM reward discovery, libretro/Stable Retro integration,
and richer FPGA differential workflow are follow-on research. No copyrighted
ROM/firmware bundled; model/data provenance and redistribution rights recorded.
Estimate 200–360 engineering hours (planning only; training convergence uncertain).
