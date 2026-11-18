# Development roadmap

[Project vision](VISION.md) preserves the long-term direction;
[Architecture](ARCHITECTURE.md) defines the boundaries. This roadmap records the
completed NES MVP, reusable machine sessions and the next AI-enabled MVP.
Milestone numbering continues from M8 so earlier references remain meaningful.

## Completed baseline: M1-M8

The first NTSC NROM MVP is complete. Its contracts and evidence remain in the
linked documents; the historical checklists are retained in Git history.

| Milestones | Delivered | Contracts and evidence |
| --- | --- | --- |
| M1-M2 | Deterministic lifecycle/timing, memory buses and bounded cartridge loading | [Core](CORE_TIMING.md), [NES memory](NES_MEMORY.md) |
| M3-M4 | Reusable NMOS 6502 with explicit Ricoh variant, and NTSC 2C02 video model | [CPU](CPU_6502.md), [PPU](PPU_2C02.md) |
| M5-M6 | NES wiring/clocks, controller input, OAM/DMC DMA, interrupts and deterministic five-channel PCM | [NES system](NES_SYSTEM.md), [APU/audio](APU_2A03.md) |
| M7 | Windows/Linux desktop, Vulkan presentation, controls, loading/reset/error recovery and Release packaging | [Desktop](DESKTOP.md) |
| M8 | Frozen legal fixtures, replay/hash checks, desktop responsiveness improvements and cross-platform acceptance | [MVP acceptance](MVP_ACCEPTANCE.md) |

Supported cartridges remain clean NTSC iNES mapper 0, 16/32 KiB PRG, CHR ROM or
CHR RAM, horizontal/vertical mirroring and one standard controller. This is a
tested subset, not a claim of full NES accuracy or broad game compatibility.

At M8 acceptance, the desktop build had 18 automated suites and the frontend-free
build had 17.
Windows ten-minute Release endurance is recorded with runtime counters. Native
Ubuntu 24.04 LTS ten-minute smoke testing and SMB1 gameplay were manually
approved by the project owner; unavailable hardware/counter details are noted
in the acceptance record. Software response observations are approximately
28-32 ms on the recorded Windows host, not a guaranteed physical latency.

## Completion rules

- Use small reviewable PRs. Complete a milestone's acceptance criteria before
  marking it done; add new scope explicitly rather than expanding a checklist
  during implementation.
- Before changing behavior, name the reference cases, expected results and
  observation boundaries. Preserve the frozen M8 fixture bytes and captures;
  investigate mismatches rather than silently updating expected values.
- Run CMake configure/build and affected CTest suites in Debug and Release.
  Keep headless core, chip models, system adapters and session code free of
  SDL3/Vulkan dependencies. Host devices and presentation belong in the frontend.
- For playback changes, compare pacing/response using the same host and
  procedure, and verify input, colors, tear-free presentation and audio
  continuity. Record results and limits; visual plausibility alone is not an
  accuracy test. Repeat platform endurance when the playback path changes.
- Keep chip responsibilities and explicit variants reusable. Board wiring,
  memory maps and clock conversion remain system-owned. Preserve specialized
  execution paths; profile before introducing performance optimizations.
- Record source/license/version for external test assets. Do not bundle game
  ROMs or proprietary firmware. Manual approvals must be identified as such.

## M9. Post-MVP cleanup

Goal: make the verified implementation easier to maintain before changing its
application boundary.

Acceptance criteria:

- [x] Remove the unused `VideoTimeline`/`VideoPacket` implementation and tests
  that only exercise the obsolete playback path. Retain coverage for the current
  latest-frame handoff and producer lifecycle.
- [x] Replace the `255` uninitialized-input sentinel with explicit state. Test
  initial input with all eight button bits set, plus reset/replacement behavior.
- [x] Extract the smoke driver and its diagnostics from application orchestration
  behind a small internal test boundary; retain the existing scripted assertions
  without exposing production ownership solely for testing.
- [x] Name timing/buffering constants and clarify locking, priming, pause,
  replacement and shutdown order. Preserve their tested values and behavior.
- [x] Put final accepted results first in validation documentation; clearly label
  historical failures and superseded builds. Keep supporting evidence and the
  README's concise build/use focus.
- [x] Existing deterministic captures and CLI behavior remain unchanged. All
  affected suites and desktop smoke checks pass with no playback regression.

Verification and limits: [M9 cleanup validation](DESKTOP.md#m9-cleanup-validation).

## M10. Common machine-session contract and NES adapter

Goal: define the small application interface needed by today's machine and a
test system, while preserving the existing NES scheduler and chip APIs.

Acceptance criteria:

- [x] Document an internal C++ session contract for reset, bounded advancement,
  timestamped input, frame/audio delivery and basic status/errors. Define clock
  units, ownership/lifetimes, absent outputs, buffering and failure behavior.
- [x] Describe outputs through system-provided metadata: rational clock rate,
  video dimensions/display aspect and pixel representation, audio format/rate,
  and supported input actions. NES palette interpretation belongs in its adapter;
  do not assume every system has a PPU, fixed frame size or NES button mask.
- [x] Implement a built-in `nes-ntsc` factory and NES session adapter. Cartridge
  rules, clock conversion, chip-specific inspection and wiring stay on the system
  side. Keep specialized diagnostics outside the common status contract.
- [x] Keep the current NES timing path intact. Interface dispatch occurs at
  session operations rather than adding a universal per-chip/per-cycle layer.
  No dynamic plugin loading or stable binary ABI is introduced.
- [x] Test the contract with both the NES adapter and a test-only fake system
  using different timing and video dimensions, including absent audio/video and
  unsupported-operation/error cases. The fake is not a second emulator product.
- [x] NES adapter runs match direct-machine state, events, frames and PCM for the
  same reset and input timeline, including different advancement chunk sizes.
  Contract tests run without SDL3/Vulkan and public interfaces are documented.

Verification and limits: [M10 session validation](SESSIONS.md#validation).

## M11. Move CLI and desktop onto machine sessions

Goal: remove NES assumptions from reusable application code while retaining the
current user experience and measured playback behavior.

Acceptance criteria:

- [x] CLI, desktop playback and audio-only playback use the common session and
  built-in factory. Generic application code no longer constructs `nes::Machine`
  or depends on CPU/PPU state types. Keep NES-specific formatting and adapters
  outside generic execution/presentation code.
- [x] Replace hardcoded NES tick conversions, frame dimensions, palette handling
  and input/media assumptions with session metadata and adapter behavior.
  Preserve the current default NES workflow and actionable cartridge errors;
  unknown system identifiers fail explicitly.
- [x] Preserve existing CLI commands, exit codes and documented NES JSON output
  through a compatibility path; do not silently redefine the existing format.
- [x] Exercise generic CLI/playback and video layout with the fake system as well
  as NES. Verify reset/replacement, unsupported capabilities, output lifetime,
  bounded queues and fault recovery without adding a public fake-system option.
- [x] Keep emulation/audio production independent of GUI waits. On the recorded
  comparison host, establish the acceptable baseline variation before changing
  pacing, then demonstrate no material response/pacing regression. Retain the
  corrected colors, aspect ratio, controls and tear-free presentation.
- [x] Frozen NES captures and Debug/Release checks pass. Extracted Windows/Linux
  Release packages pass the scripted flow and ten-minute audio-continuity check.
  Record any owner-approved manual evidence and measurement limits explicitly.
- [x] Update architecture/build/use documentation to match the implemented
  boundary. Adding another built-in adapter should not require rewriting generic
  frontend timing, video upload or audio production.

Verification: [M11 validation](M11_VALIDATION.md).

## Configuration approach

M10-M11 introduce statically registered C++ factories; the validated C++ NES
definition remains authoritative. Selecting `nes-ntsc` is sufficient for this phase.

Application preferences (controls, window/audio settings) and executable machine
definitions are separate concerns. JSON may be used for either later, but these
milestones do not add a settings framework or a machine-description parser.

Introduce versioned JSON machine manifests only when another real system or
custom-machine use case establishes the required chip variants, clocks, wiring,
memory maps and boot contract. Registered C++ components still implement chip
behavior. A future loader must reject invalid or unsupported combinations and
prove equivalence with the corresponding validated built-in definition.

## Next MVP: M12–M20

M13 automation and external API are complete. M14 full state and replay are
next in the [AI MVP roadmap](AI_MVP_ROADMAP.md). Preserve the M11 validation
baseline and use [release acceptance](AI_MVP_ACCEPTANCE.md) for the final AI MVP.

## Later work: scope separately

The following directions remain visible in [Project vision](VISION.md). Only
inspection, snapshots and replay explicitly scoped in the AI roadmap are part
of this MVP; other items require separate acceptance gates.

- Broader NES compatibility: NES 2.0, additional mappers/board variants, atomic
  battery saves, regions/peripherals and compatibility-driven unofficial opcodes.
- Proven chip reuse in another real machine, with Apple IIe a candidate; educational
  and experimental machines follow explicit component and boot contracts.
- Diagnostics: richer headless captures/traces, non-mutating inspection,
  save states/rewind and reproducible Retro Silicon FPGA comparison.
- Validated machine manifests and a visual workbench for typed connections,
  clocks, memory maps and chip/signal inspection.
- Other systems, GPU compute acceleration and optional signal-level models only
  when separately scoped tests demonstrate their value. No FPGA equivalence or
  analog-output accuracy claim follows from completing the NES MVP.
