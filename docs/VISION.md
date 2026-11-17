# Project vision

Retro Silicon Emulator is an open, modular, chip-oriented emulation framework
and desktop application. It has three complementary long-term goals:

- Preserve historical computers through accurate, reusable models of CPUs,
  video/audio chips, memory, controllers, buses, and glue logic.
- Let developers construct experimental computers from historical and new parts,
  with explicit adapters when bus, timing, electrical, or firmware contracts differ.
- Enable machine-independent observation and control for AI learning, automated
  software testing and hardware verification across historical and custom machines.

The first usable product is an NTSC NES emulator. A working machine comes before
a generic framework demonstration. Apple IIe is a later target for proving reuse;
educational computers, other consoles, and a visual workbench extend that goal.
Build abstractions from real machine requirements, not speculative universality.

This document preserves the long-term intent of the original 2026-11-13 technical
specification. [Architecture](ARCHITECTURE.md) describes design boundaries and
current contracts. [Roadmap](ROADMAP.md) records the completed first MVP and the
current milestones and acceptance gates. Future directions here are not additional
milestone requirements or claims of implemented functionality.

## Product surfaces

| Surface | Responsibility |
| --- | --- |
| Reusable C++ libraries | Chip models, capabilities, buses, timing, machine definitions, state and traces |
| Headless CLI | Bounded execution, deterministic tests, inspection, captures, tracing, benchmarks, and FPGA comparison |
| Desktop app | SDL3 window/input/audio endpoints; Vulkan presentation and visualization |
| Future workbench | Visual composition, typed wiring, validation, and chip inspection |

Desktop and CLI should share machine-session operations and consume the same
logical outputs. Neither presentation nor an audio callback owns chip execution.
Headless operation must work without a display, SDL3, Vulkan, or an audio device.

## Reusable parts and compatible machines

Prefer small capability interfaces over a universal chip interface. Model each
part's observable responsibilities and explicit variants. The machine owns device
lifetimes, address decoding, clock ratios/phases, reset order, interrupt routing,
DMA ownership, and integration policy. Avoid global singletons.

Substitution requires compatible address/data widths, bus transactions, timing,
reset behavior, interrupt signals, and firmware expectations. Metadata can describe
component identity/version, variants, clock constraints, capabilities, and state
schema. Adapters must make differences explicit; interchangeable APIs do not prove
historical equivalence. Do not implement a metadata framework until a real use case
needs it. Start with static registration, not a dynamic binary plugin ABI.

Future versioned machine manifests can describe component selection, wiring,
clocks, memory maps, and initialization. The built-in C++ NES definition remains
authoritative until executable manifest validation is proven. The workbench may
add a component palette, typed connections, compatibility warnings, memory-map
editor, clock graph, signal viewer, and machine save/load. Every executable custom
machine needs an explicit boot and firmware contract.

## Deterministic execution and observation

The same ROM, initial state, configuration, input timeline, and emulator version
should produce identical logical state and canonical output hashes on supported
builds. Document timing boundaries and equal-time phases. Do not let incidental
component order, host scheduling, GPU completion, or audio buffering set hardware
timing. Integer clocks and explicit bus side effects are foundational.

Bounded run-until-tick, frame, instruction, and device-event operations are intended
session capabilities. A frame is a device event, not necessarily a fixed tick count.
Chip output and host presentation remain separate: indexed video plus metadata,
explicit palette conversion, and deterministic internal audio with a documented
mixer/resampler. Analog-output approximations require their own stated limits.

Use bounded queues between emulation and presentation/audio consumers, with
explicit overflow, underrun, pause, speed-change, and stepping policies. Input
becomes timestamped emulation events. Host callbacks must not mutate chip state.
Profile headless throughput on recorded build/hardware configurations; optimize
only with evidence that reference behavior is preserved. No universal FPS promise.

## Verification and the Retro Silicon FPGA project

Software and FPGA implementations complement each other but remain independent.
They can share specifications, licensed fixtures, trace schemas, and verification
methods without sharing implementation structure or the same untested assumptions.
Software is a reference candidate, not automatic ground truth.

A future differential workflow should match ROM hashes and memory initialization,
align reset/clock phases, apply identical timestamped inputs, compare instruction
state then bus/PPU events, and report the first divergence with bounded context.
Minimize reproductions. Keep capture adapters outside the emulator core. Initially
favor a versioned, inspectable trace format such as JSON Lines; define clock
domains, pre/post-edge meaning, event ordering, and provenance before freezing it.

Test assets need a source, license, version/hash, expected result, and machine
profile. Combine unit, conformance, integration, differential, parser/state fuzz,
sanitizer, and performance tests as the relevant features arrive. Every fixed
compatibility bug should gain a deterministic regression case.

## Diagnostics and saved state

Future debug inspection must be distinct from side-effecting device reads.
Specify instruction/cycle/dot/event stop boundaries. Desired tools include CPU
registers, disassembly, breakpoints/watchpoints, memory inspection, PPU/APU views,
and event timelines. Tracing and logging should be independently configurable.

Snapshots must include all relevant chip state, pending events/interrupts, bus
latches, DMA, mapper state, and input latches. Version them with machine profile,
ROM hash, component versions, and integrity checks; reject incompatible restores.
Never persist raw pointers, host handles, GPU objects, or other platform resources.
CLI captures/reports should record duration, configuration, emulator version,
palette/sample settings, hashes, and outcomes. CLI schemas and exit codes become
compatibility commitments only when defined and implemented.

## Beyond the first MVP

Broader NES releases can add NES 2.0, MMC1/MMC3 and board variants, save RAM,
compatibility-relevant unofficial opcodes, additional regions/peripherals, richer
desktop controls, snapshots, diagnostics, and FPGA comparison. Battery-backed
data should be written atomically outside the source ROM. Keep test results and
limitations explicit and ship tested Windows/Linux installation packages.

Apple IIe, additional CPU variants, disk controllers, firmware strategy, custom
machines, and the visual workbench follow proven component reuse. macOS is a later
portability possibility, subject to the chosen Vulkan tooling. No automatic HDL
translation, arbitrary plug-and-play electrical compatibility, full transistor
simulation, network play, online ROM distribution, achievements, cloud services,
or embedded scripting is required for the initial releases.

## Intelligence and automation

Emulate. Create. Train. Verify. The proposed [second MVP](AI_MVP_ROADMAP.md)
adds optional AI SDK/training and native model execution, initially for NES NROM.
Agents observe memory, registers, events, video and audio as supported, then
submit timestamped input at defined boundaries. The same interface serves
traditional test tools and future workbench machines. Task rewards and models
remain external to hardware; arbitrary application competence is a research goal,
not a release promise. CPU simulation remains the timing authority while CUDA
accelerates learning and inference. Support both human-visible real-time operation
and parallel accelerated headless runs.
