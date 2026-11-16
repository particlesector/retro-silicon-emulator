# Machine sessions (M10)

The internal C++ application boundary is [Session](../include/rse/session/session.hpp).
It is source-level code, not a plugin ABI, serialized state format or universal
chip interface. `rse_session` has no system, SDL3 or Vulkan dependency. The
`rse_nes_session` adapter and `rse_builtin_sessions` factory are headless libraries.
CLI and desktop continue using their existing paths until M11.

## Contract

- `info()` returns immutable system metadata, whose strings/spans live until
  session destruction. Clock frequency is a positive rational number of master
  ticks per second; tick units are system-specific, never host milliseconds.
- `advance_to(t)` executes through the inclusive target. Equal time is a no-op;
  rewinds are invalid. `run_frame()` is optional: an adapter advertising it must
  reach its next publication in a finite system-defined interval or throw.
  It is unavailable on systems without that capability; frame lengths need not
  be constant. No virtual dispatch is added inside a chip clock or bus access.
- Input supplies a complete set of pressed action names for one named port at
  a strictly future tick. Names are defined by metadata, not a global button mask.
  Calls copy/translate input immediately, so caller storage need not survive.
  Unknown/duplicate actions, unknown ports and invalid times are rejected before
  modifying the machine. Equal-tick accepted events preserve insertion order.
- `video()` is absent until the first completed frame or when there is no video.
  It borrows the latest frame, with explicit byte pitch and pixel representation.
  Copy it before the next non-const operation or session destruction. Raw views
  are native in-process storage, not portable file encodings. `convert_video()`
  fills exactly width*height caller-owned RGBA pixels without advancing time;
  adapters own palette interpretation. Missing video is unsupported; conversion
  before publication is a logic error. Completed frames may replace older ones.
- Audio is owning, interleaved native signed 16-bit PCM, with rate/channel count
  and sample queue bound in metadata. `take_audio()` drains without advancing;
  an empty block means no pending samples. An absent audio capability throws
  `UnsupportedOperation`, rather than pretending it produced silence.
- Validation errors use `invalid_argument`; unsupported capabilities use
  `UnsupportedOperation`. Media I/O and execution errors retain their descriptive
  exception types. Execution/buffer failures set `status().faulted`; advancing or
  scheduling more input then requires reset. Inspection and draining partial PCM
  do not clear the fault. Partial output is diagnostic, not a completed run.
- Reset reconstructs deterministic power-on state, clears scheduled input,
  frames and queued PCM, and preserves configuration/metadata. Callers serialize
  all operations and manage host threads, queues and devices outside sessions.

## NES built-in

`session::create("nes-ntsc", image)` copies and validates bytes using the existing
cartridge parser. `session::load("nes-ntsc", path)` uses its bounded file loader.
Unknown IDs fail before media access; `builtin_ids()` lists only real built-ins.
Factories are static C++; no JSON configuration or dynamic registration is added.

The NES adapter delegates to the existing specialized `nes::Machine`, retaining
clock phases, memory maps, CPU variant, DMA and interrupt ordering. It reports
236,250,000/11 master Hz, 256x240 indexed native-U16 frames with 4:3 display aspect,
and 48 kHz mono PCM. Its `controller-1` actions are A/B/select/start/directions
(lowercase IDs). The adapter maps them to existing hardware wiring internally.
The read-only `nes::Session::diagnostics()` retains chip state, memory inspection
and trace details outside the common status contract.

The adapter always captures audio; its NES profile supplies the sample limit
(default 48,000 individual samples). Overflow faults rather than dropping PCM.
Drain regularly during long runs. Input scheduling retains the existing machine's
allocated event timeline; callers bound future-event volume. No extra frame FIFO
is introduced. The accepted approximate RGB palette/emphasis conversion is shared
by the adapter and existing desktop wrapper; this is not an analog decoder.

## Reference and acceptance cases

`session-tests` checks the contract using the three frozen M8 programs and a
test-only fake with different clock/video/audio metadata and optional outputs.
Expected NES results are exact equality with direct-machine state/RAM, trace,
indexed frames and PCM across the same input timeline, chunked advancement and
reset. Invalid requests must not mutate state; faults require reset; copied
output must survive reset. Palette extraction must preserve all 512 RGBA results.
The fake is not registered as a public system. Existing frozen fixture tests,
CLI behavior and chip reference cases remain required and unchanged.

## Validation

On 2026-11-15, Windows MSVC 19.44 Debug and Release and Ubuntu 24.04 WSL
GCC 13.3 Release passed all 19 desktop-enabled CTest suites. The final added
input atomicity and image ownership cases also passed Windows Debug. Frozen
M8 fixture bytes and captures were unchanged. All 512 palette/emphasis RGBA
values retain the pre-extraction FNV-1a hash `eb035541bc4109e5`.

The Linux Vulkan/Xvfb scripted desktop check uses dummy audio; it verifies the
existing application flow and renderer, not audible continuity or physical input
latency. CLI/desktop execution migration and repeated real-device endurance
remain M11 work. Session contract tests themselves require neither SDL nor Vulkan.
