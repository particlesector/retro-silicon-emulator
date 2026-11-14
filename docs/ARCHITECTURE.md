# Retro Silicon Emulator architecture

## Goals
Build an accurate NES emulator first, with a reusable component-based C++23 core suitable for future historical and experimental machines. Support deterministic headless tests and comparison with Retro Silicon FPGA traces.

## Layers
- `rse_core`: platform-independent components, scheduler, buses, machine lifecycle and state management.
- `rse_nes` (`systems/nes`): cartridge loading and NES memory mapping; Ricoh
  2A03, PPU, APU, and device integration follow in later milestones. See
  [NES memory and cartridge loading](NES_MEMORY.md).
- `rse-cli`: headless execution, tracing, snapshots and differential testing (planned).
- `rse-desktop`: optional SDL3 window/input/audio and Vulkan renderer.

## Architectural invariants
- No SDL3 or Vulkan dependencies in `rse_core`.
- Machine time is monotonic and represented using integer ticks; cross-domain clock semantics must be defined before implementing NES timing.
- Component boundaries and bus side effects must be explicit.
- Trace formats will specify clock domain, ordering, schema version and initial state.
- Hardware and software models are independently implemented; shared tests and specifications provide verification.

## Scaffold status
The machine advances components at each integer tick in registration order;
see [Core timing and lifecycle](CORE_TIMING.md) for the contract and verification
boundaries. It is **not** an NES emulator. Desktop creates a window but has no
Vulkan swapchain or rendering yet.

## Next milestone
Follow the [MVP roadmap](ROADMAP.md): core timing and lifecycle, then the memory
bus and cartridge loader, followed by a tested 2A03 CPU model.

## Simulation fidelity and execution speed

Simulation time is independent of host wall-clock time. A deterministic scheduler owns integer simulated timestamps, ordering of simultaneous events, and synchronization among clock domains. A separate execution controller supports real-time pacing, slow motion, fast-forward, unlimited throughput, pause, and stepping by instruction, CPU cycle, PPU dot, scanline, frame, or selected event.

Three *capabilities*, not universally interchangeable runtime switches, are envisioned:
- Functional models: preserve required externally observable behavior with fewer intermediate events.
- Cycle-accurate models: preserve defined bus transactions, interrupt, DMA, and device timing at documented boundaries.
- Optional signal-level models: resolve pins, multi-bit buses, clock edges, tri-state contention, and propagation semantics for educational and diagnostic use.

The NES is the first cycle-accurate target; Ben Eater-style SAP and W65C02 breadboard systems are later modularity demonstrations. Saturn and N64 are long-term targets with substantially greater multi-processor, memory, and graphics synchronization requirements. Model capabilities and state-transfer compatibility must be declared explicitly; switching fidelity mid-run is not assumed safe.

## Hybrid CPU/GPU acceleration

The scheduler remains the sole authority for **simulated** hardware time. Vulkan is optional and must not become a dependency of the platform-independent core. A compute-backend abstraction may allow selected components to delegate expensive, sufficiently independent workloads to CPU or Vulkan compute implementations.

Good candidates include N64 RDP-like rasterization, selected Saturn VDP workloads, parallel signal batches, and offline trace comparisons. CPU instruction sequencing, tightly coupled bus arbitration, DMA, and interrupt coordination initially remain CPU-side. GPU dispatch, synchronization, and readback overhead can exceed the savings for fine-grained work; benchmark before introducing acceleration.

An accelerated job consumes an explicitly versioned input/state snapshot and produces results with a defined simulated completion event. Host GPU completion time must **never** determine emulated completion time. The host waits when results are required; GPU work cannot race with observable reads/writes or change event order. Record memory visibility, side effects, and synchronization points precisely.

### Proposed interfaces (design only)

- `IComputeBackend`: backend identity, capability metadata, submission and completion/error reporting.
- `ComputeJob`: immutable input snapshot, operation kind, deterministic parameters, and required simulated completion timestamp.
- `ComputeResult`: output/state delta, error/status, and trace correlation identifier.
- `IHardwareModel`: documented observable timing and optional backend selection; CPU reference implementation is the baseline.

Do not force all hardware models into a generic GPU kernel interface. The backend boundary should exist only where it offers measurable value.

### Verification requirements

1. Reference CPU backend and accelerated backend share behavioral conformance tests.
2. Compare timestamped bus transactions, interrupt/DMA behavior, output hashes, and relevant state checkpoints against reference traces.
3. Include cases with mid-frame register writes, memory contention, readbacks, interrupts, and state save/restore.
4. Preserve reproducible results across supported hardware/drivers where the backend claims deterministic operation; explicitly document unsupported cases.
5. Provide a reference-only verification mode and a separately documented performance mode. Rendering accuracy alone is not evidence of cycle-accurate device timing.
6. Track host throughput separately from simulated cycles, and profile CPU/GPU synchronization overhead.

### Incremental roadmap

- NES: deterministic CPU scheduler, bus semantics, cycle/dot tracing, and headless FPGA comparison. Vulkan initially handles presentation only.
- Educational machines: optional signal-level components and inspectable wiring without slowing ordinary chip-level execution.
- Advanced consoles: first establish CPU reference models and exact device timing contracts; profile hotspots; accelerate suitable graphics/compute operations behind validated backends.
- Only consider generalized GPU signal simulation if measured workloads justify it.

## Accuracy boundaries

Cycle accuracy is always relative to a **named observation boundary** (for example CPU bus cycles, PPU dots, DMA events, or chip pins). It does not imply transistor-level or analog accuracy. FPGA trace comparison requires matched initial state, clock-domain mapping, input events, and documented handling of undefined hardware behavior.
