# Retro Silicon Emulator architecture

## Goals
Build an accurate NES emulator first, with a reusable component-based C++23 core suitable for future historical and experimental machines. Support deterministic headless tests and comparison with Retro Silicon FPGA traces.

Historical preservation and experimental computer construction are equally
important long-term goals. The NES is the first concrete product; Apple IIe and
custom machines should reuse chip models with explicit adapters and machine-owned
wiring. See [Project vision](VISION.md) for the fuller product direction, component
compatibility, workbench, diagnostics, and FPGA verification goals. The
[MVP roadmap](ROADMAP.md) remains the authority for first MVP scope.

## Layers
- `rse_core`: platform-independent components, scheduler, buses, machine lifecycle and state management.
- `rse_cpu6502`: reusable NMOS 6502 instruction engine with explicit CPU variants,
  independent of NES memory maps, audio, DMA, and frontend APIs. See
  [6502 CPU contract](CPU_6502.md).
- `rse_ppu2c02`: reusable NTSC PPU register, rendering, palette, and OAM model;
  external memory is supplied by the machine. See [2C02 contract](PPU_2C02.md).
- `rse_devices`: reusable logical peripherals, including the CD4021 shift register,
  standard controller wiring, and 2A03 OAM DMA sequencer.
- `rse_nes` (`systems/nes`): cartridge loading, NES memory mapping, and a Ricoh CPU
  variant adapter, PPU memory wiring, and an NTSC machine runtime with controller
  and OAM DMA integration. See [NES memory](NES_MEMORY.md) and
  [system timing and execution](NES_SYSTEM.md). APU/DMC integration follows in M6.
- `rse-cli`: headless execution, tracing, snapshots and differential testing (planned).
- `rse-desktop`: optional SDL3 window/input/audio and Vulkan renderer.

## Architectural invariants
- No SDL3 or Vulkan dependencies in `rse_core`.
- Machine time is monotonic and represented using integer ticks; cross-domain clock semantics must be defined before implementing NES timing.
- Component boundaries and bus side effects must be explicit.
- Reusable parts follow chip-level responsibilities, with explicit variants for
  hardware differences. Board wiring and clock conversion belong to system models.
  Preserve tested observable behavior using the least expensive suitable model;
  profile optimizations and add signal-level detail only where it has verified value.
- Trace formats will specify clock domain, ordering, schema version and initial state.
- Hardware and software models are independently implemented; shared tests and specifications provide verification.

## System and application boundaries

Machine definitions own device graphs, address decoding, clock ratios/phases,
reset sequencing, interrupt routing, and DMA arbitration. The M1 registration
order is a deterministic baseline; it does not establish hardware phase ordering.
NES integration must define and test that ordering explicitly, including variable
PPU frame lengths. Do not freeze a scheduler or universal chip API in advance of
those requirements.

The original NTSC proposal uses a PPU-dot timeline with three dots per CPU cycle
and explicit rendering-time odd-frame dot skipping. Validate the ratios, phases,
and skipped-dot handling during NES integration; use a finer master unit if needed
to represent required edges exactly. Equal-time events need documented phases or
priorities and a stable tie-breaker, rather than accidental container ordering.

Bus/device contracts must eventually distinguish driven bits from floating bits,
resolve open-bus values, and identify access timing, ownership, and side effects.
The CPU bus sees the resolved byte; partially driven device results can be merged
by the system bus. Side-effect-free inspection must not accidentally clear status
flags or advance devices. M2's zero-read policy is a documented placeholder.

Desktop and CLI should use common machine-session commands and receive frames,
audio, diagnostics, and state/trace artifacts through an application boundary.
The UI must not drive chip internals directly. Keep host callbacks and bounded
presentation queues outside logical simulation; define backpressure explicitly.
Version future state/trace formats and validate compatibility before loading.

## Implementation boundaries
The generic runtime provides the [M1 lifecycle contract](CORE_TIMING.md). The NES
runtime owns master-clock scheduling and has verified headless execution of the
generated NROM fixtures in [M5](NES_SYSTEM.md). This does not establish broad ROM
compatibility. APU/audio and desktop presentation remain later work; desktop
still creates a window without a Vulkan swapchain or frame presentation.

## Next milestone
Follow the [MVP roadmap](ROADMAP.md) and its acceptance gates for remaining work.

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
