# Inspection and automation

`Session::inspection()` returns an optional interface owned by the session.
Serialize calls with session execution. NES exposes its schema through
`schema()`. IDs and metadata views have static lifetime in the NES adapter.
The API is an internal C++ source contract.

## Reads

Discover value IDs, widths, clock domains, address spaces and extents through
the schema. Memory uses byte addresses. Scalars use address zero.
`peek()` and `read()` validate IDs and ranges before reading. A missing value
is represented by `nullopt`, for example an unmapped CPU address. An unknown ID
throws `invalid_argument` and a range outside the domain throws `out_of_range`.

- `cpu.ram`, `ppu.nametable-ram` and `ppu.oam` expose physical storage.
- `cpu.bus` includes RAM, NROM and PPU register mirroring. Device peeks report
  the current read value without updating the CPU latch, acknowledging IRQ,
  clocking the controller, clearing vblank, or changing the PPUDATA read buffer
  and address. Unmapped CPU regions return an unavailable value.
- `ppu.bus` includes CHR, nametable mirroring and raw palette aliases. The
  CPU-facing PPUDATA peek applies the chip's buffer and grayscale behavior.
- Registers and signals report the current model state at the session's tick.

## Subscriptions

Each NES session has one subscription. Select events by ID, mapped address
range, or both. Select a sampling boundary independently and optionally
provide a value range for change records. The first sample emits all values
in ascending address order with `initial` set. Later samples emit changes
against the previous sample, including changes involving unavailable values.
A sampled boundary emits a marker when no delta range is configured.

| Boundary | Before and after enclose | Record index |
| --- | --- | --- |
| Master tick | All work at each integer tick advanced through, including ticks with no clock edge | Master tick |
| CPU cycle | APU clock, interrupt routing, CPU slot and PCM production, including DMA holds | CPU slot |
| Instruction | The CPU slot whose pre- or post-clock state reports an instruction boundary | CPU slot |
| Scanline | PPU processing of dot 0 of a scanline | PPU dot count, one-based |
| Frame | PPU processing of the publication dot | Frame number |

The instruction boundary follows the CPU core's boundary predicate, which
includes reset and hardware interrupt entry completion. At a shared tick,
master-before comes first, then scheduled actions in insertion order, PPU work,
CPU work and master-after. Local before/after markers enclose their respective
work. Events are recorded at the existing trace emission points. Bus events
follow the access, so a nested PPU bus access can precede its enclosing CPU bus
event. The event schema describes which events carry a mapped address and
which native clock supplies their index. The frame event uses its published
frame number as the index.

Subscribe reserves space for the requested positive capacity. While full, the
stream drops new records and keeps counting both sequence IDs and losses.
Draining returns owned records and the loss count since the prior drain.
Sequence IDs start at one and continue across drains, including gaps for lost
records. Delta tracking continues through overflow. Resubscribe to obtain a
fresh initial sample after loss. Reset and unsubscribe discard the active
subscription and its pending records. Invalid replacement preserves the
previous subscription. There are no callbacks during emulation.

## Automation controller

Link `rse_automation` and give `rse::session::Automation` exclusive ownership
of a session. Built-in sessions can be created or loaded with
`rse_builtin_sessions`. The automation library has no frontend or Python
dependency. Its status, schema and inspection APIs report the adapter's
capabilities and current committed state.

## Commands and stop points

- `run` advances towards an inclusive master-tick target using a positive
  budget. Execution stops at the earlier of target and budget, unless a stop
  condition fires first. An equal target performs no work. Rewinds are rejected.
- `step` in a run request selects the next supported boundary. A breakpoint
  compares one inspected value with an unsigned value at a named boundary.
  Choose step or breakpoint. A watchpoint matches a bus event and a mapped
  address range and can be combined with either control.
- Stops commit the whole matching master tick. Breakpoint values are sampled at
  the after phase of the selected boundary. Matching records are examined in
  stream order. Breakpoints are checked on future boundaries, so resuming can
  reach another boundary even if the current value already matches.
- `pause` can be called while another command is executing. Uncontrolled runs
  check it between chunks of at most 256 master ticks. Runs with stop controls
  check it before each tick. Pause remains set through reset until `resume`.
  A pause after the last check can leave an already reached target or stop
  condition as the run result.
- All other commands are serialized. Overlapping and reentrant calls fail
  immediately. Timestamped inputs use the session's named ports/actions and
  must be strictly in the future. Session reset clears the input timeline.
  Native execution failures propagate through the session contract.
- Drain PCM periodically when running audio-capable sessions. Their existing
  PCM queue limits apply. The automation layer does not pace against wall time.

## Binary observation batches

Positive capacity enables output for the request. The transport retains at most
65,536 records per request. It streams session events and includes markers or
value deltas emitted by the requested stop control. With capacity zero, stop
controls still work and the returned byte buffer is empty.

Every run starts a new subscription and its sequence IDs start at one. Lossless
delivery returns an explicit `overflow` stop at the first chunk with loss,
with the committed tick, retained prefix and loss count. The chunk can contain
multiple events and its execution is already committed. Preview continues and
counts dropped records. If the underlying stream loses records during a run
with stop controls, either mode returns `overflow`, since a stop may have been
missed. Clients can lower their per-request tick budget or increase capacity
to avoid loss. Buffers returned by native C++ and Python are owned by the client.

The layout below uses little-endian unsigned fields with no padding.

| Header field | Bytes |
| --- | --- |
| ASCII `RSEO` | 4 |
| Format version, currently 1 | 2 |
| Reserved, zero | 2 |
| Record count | 4 |
| Lost count | 8 |

| Record field | Bytes |
| --- | --- |
| Sequence, master tick, native index | 8 each |
| Kind (event 0, boundary 1, delta 2) | 1 |
| Phase (before 0, after 1) | 1 |
| Flags (address, value, previous, initial in bits 0–3) | 1 |
| ID byte count | 1 |
| Address | 4 |
| Value and previous value | 8 each |
| ASCII schema ID, empty for boundary markers | ID byte count |

Absent fields contain zero. Discover value and event metadata with
`Automation::schema()` and resolve record IDs against that schema. The
[subscription contract](#subscriptions) defines stable IDs, phases and indices.
Binary delivery requires non-NUL ASCII IDs at most 255 bytes long. Incompatible
schema IDs are rejected before execution.

## Optional Python bridge

Configure with `-DRSE_BUILD_PYTHON=ON` and build `rse_python_bridge`. Add
`bindings/python` to the Python module path. Pass the shared library path and
ROM bytes to `rse_automation.Session`. The bridge uses the standard library's
`ctypes.CDLL`, which releases the GIL around native calls.

```python
from pathlib import Path
from rse_automation import Session, decode_observations

with Session(bridge_path, Path(rom_path).read_bytes()) as session:
    print(session.schema()["input_ports"])
    session.schedule_input(12, "controller-1", ["start"])
    tick, reason, batch = session.run(1000, 1000, capacity=512)
    lost, records = decode_observations(batch)
```

`schema()` includes the system ID, rational clock, input ports, inspection
values and events, and supported boundary names. The value descriptors contain
kind, clock, width, extent and address space. Event descriptors contain clock,
address domain and width. Kinds are memory (0), register (1) and signal (2).
Clocks are master (0), CPU (1) and video (2). Address spaces are physical (0)
and mapped (1) for memory entries. A Python breakpoint is
`(boundary, value_id, address, equals)` and a watchpoint is
`(event_id, address_domain, first, count)`.

The Python session retains its native handle through each call. Closing during
a command prevents new calls and defers destruction until the existing call
returns. Use pause to interrupt a long run before closing. Observation and PCM
results are Python-owned bytes and can outlive the session. The bridge is a
local implementation detail of the Python module.

## Parallel headless sessions

`rse_batch` owns a fixed number of independent C++ sessions. Link
`rse_builtin_sessions` to supply the NES factory. Batch execution runs
without host display, audio, wall-clock pacing or Python dependencies.

The caller supplies a factory that creates a fresh session from an index and a
seed. Each slot gets a SplitMix64-derived seed from the batch seed and its
index. The factory gets that same seed when the slot resets, unless the caller
supplies a new one. NES power-on state is deterministic and has no machine
RNG. A task can use the seed to choose initial conditions or generate inputs.
The factory runs on the calling thread and must supply independently owned
machine state. Immutable ROM bytes may be shared by the factory.

A batch allows up to 256 sessions and up to 64 workers, with at most one worker
per session. One worker uses a serial loop on the calling thread. A larger
pool uses fixed threads and distributes slots dynamically. The order in which
workers finish cannot change result ordering or timestamped input. Commands
are synchronous and competing or reentrant commands fail immediately.

## Steps and observations

- `step(ticks, actions)` advances each slot by the specified positive master
  tick count. An optional action supplies the complete pressed set for one
  port and takes effect on that slot's first tick. An omitted action leaves
  input unchanged. Each slot has its own tick cursor.
- Results are returned in environment order with status, configured read
  ranges and an optional copy of the latest published frame. Reads flatten
  into the configured range order, with unavailable values represented by
  `nullopt`. The API supports up to 4,096 values and a 4 MiB frame per slot.
- Malformed batch shape is rejected before any slot advances. A slot's input,
  execution or output failure is reported in that slot's result. Successful
  slots still advance. An output-copy error may follow committed advancement.
  Reset a faulted slot before running it again.
- Host PCM is drained and discarded. A pool with video copies disabled still
  runs the PPU and publishes emulated frames internally. The APU, DMA, CPU,
  rendering pipeline and deterministic PCM converter continue to run.
- `snapshot(index)` produces the slot's canonical machine state whenever
  supported, including the emulated state used by the video and audio paths.

## Reproducing the benchmark

Run `rse-batch-tests --benchmark <environments> <workers> <steps>` from a
Release build in a fresh process for each row. Use the same environment count
with one worker to obtain a serial hash. Run the tests without arguments for
per-step observation comparison as well as fault isolation and reset coverage.

The benchmark uses the frozen AV fixture, the same seeded action policy in
each variant, 357,368 master ticks per step, 32 RAM values and a copy of each
slot's latest indexed frame. The workload does not submit GPU work.
Columns are environments, workers, elapsed seconds, aggregate published
frames/s, master ticks/s, slot steps/s, process CPU percent, peak resident
KiB, logical observation MiB/s and aggregate canonical state hash. CPU uses
100% for one logical core. Bandwidth counts frame bytes and nine logical bytes
per optional 64-bit observation. Peak resident memory includes process setup.
