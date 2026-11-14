# Retro Silicon Emulator architecture

## Goals
Build an accurate NES emulator first, with a reusable component-based C++23 core suitable for future historical and experimental machines. Support deterministic headless tests and comparison with Retro Silicon FPGA traces.

## Layers
- `rse_core`: platform-independent components, scheduler, buses, machine lifecycle and state management.
- `systems/nes` (planned): Ricoh 2A03, PPU, APU, cartridge and mapper integration.
- `rse-cli`: headless execution, tracing, snapshots and differential testing (planned).
- `rse-desktop`: optional SDL3 window/input/audio and Vulkan renderer.

## Architectural invariants
- No SDL3 or Vulkan dependencies in `rse_core`.
- Machine time is monotonic and represented using integer ticks; cross-domain clock semantics must be defined before implementing NES timing.
- Component boundaries and bus side effects must be explicit.
- Trace formats will specify clock domain, ordering, schema version and initial state.
- Hardware and software models are independently implemented; shared tests and specifications provide verification.

## Scaffold status
The current machine simply advances placeholder components to a requested tick. It is **not** a cycle-accurate scheduler or an NES emulator. Desktop creates a window but has no Vulkan swapchain or rendering yet.

## Next milestone
Define scheduler semantics, bus transactions, component lifecycle and error handling; then implement a tested 2A03 CPU model.
