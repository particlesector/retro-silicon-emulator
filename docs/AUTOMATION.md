# Session automation

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
[inspection contract](INSPECTION.md) defines stable IDs, phases and indices.
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

## Validation

- Windows MSVC 19.44 desktop builds passed 21 CTest suites each in Debug and
  Release with Python 3.13.3.
- Ubuntu 24.04 WSL with GCC 13.3 passed 21 suites in desktop Release with
  Python 3.12.3 and 19 in headless Debug with the bridge disabled.
- The native suite checks a fake session boundary, NES instruction and bus
  stop ticks, command reentrancy, input validation, overflow and identical
  observation bytes and PCM on replay. The Python suite checks concurrent
  pause and close, owned buffers and malformed requests and batches.
- Existing frozen M8 fixture and replay checks passed in all configurations.
