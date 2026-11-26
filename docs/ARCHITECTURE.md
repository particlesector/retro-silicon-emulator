# Architecture

| Layer | Owns | Entry points |
| --- | --- | --- |
| Core and chips | Integer simulation time, lifecycle, CPU and peripheral state, bus transactions | [Core](../include/rse/core), [CPU](../include/rse/cpu), [video](../include/rse/video), [audio](../include/rse/audio) |
| System | Chip selection, memory map, clock conversion, reset, interrupt and DMA routing | [NES model and headless CLI](NES_SYSTEM.md) |
| Session | Built-in machine selection, bounded execution, timestamped input, completed frames and PCM | [Machine sessions](SESSIONS.md), [inspection and automation](AUTOMATION.md) |
| Applications | CLI reporting, SDL3 input/audio, Vulkan presentation and host pacing | [Desktop controls and packages](DESKTOP.md) |
| Optional learning tools | Gymnasium environments, task rewards, checkpoints and policy inference | [Training and playback](TRAINING_SDK.md) |

## Time and ownership

The generic machine owns its components and dispatches integer ticks in registration order. Registration closes on the first positive advance. Reset establishes tick zero. Advance processes (current, target], so repeated calls to the same target perform no work. On a callback failure the committed tick stays at the last completed boundary. Components may have partial state and the machine requires a successful reset to resume. [Core interfaces](../include/rse/core/machine.hpp)

A system with a specialized scheduler defines device phases and equal-time ordering explicitly. For a fixed initial state and input timeline, changing caller step sizes must preserve state and outputs. Chip models own observable behavior. Board wiring, address decoding and host file paths stay outside reusable CPUs and peripherals.

Applications own threads, queues, windows and devices. The producer copies a published frame before the next mutating session call. Host scheduling sets playback pace while the machine determines simulated time. Built-in systems are statically registered C++ adapters. Inspection, snapshots and training depend on capabilities exposed by each adapter. [Session interfaces](../include/rse/session/session.hpp) · [Built-in factory](../src/session/builtins.cpp)

Design new abstractions around a demonstrated machine requirement. The GBA introduces firmware alongside cartridge media and a persistent flash device. The planned ownership and acceptance tests are in the [GBA roadmap](GBA_ROADMAP.md).
