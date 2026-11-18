# Machine-session inspection

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

## Validation

The frozen M8 replay suite compares state, trace, video and PCM hashes across
unobserved frame stepping and chunked advancement with observation on and off.
The observed run combines event streaming with CPU-cycle NMI deltas. It compares
each event with its trace entry, including OAM/DMC DMA and interrupts, and
checks the sampled NMI state. Session tests cover all five boundaries, equal-tick
input order, chunk independence, mirroring, device peeks, deltas and overflow.

Run `rse-acceptance-tests --benchmark-disabled` from a Release build for the
paired same-executable timing check. Each pair runs 240 frozen AV fixture frames
with PCM capture and drain. The first column is the default unsubscribed
baseline and the second is the same workload after subscribing and disabling
observation. Timing excludes subscription setup and teardown. Both runs use the
same idle code path, so a comparison with the pre-M12 executable is needed to
estimate the cost of adding the idle hooks.

On the Windows development host (Ryzen AI Max+ 395, MSVC 19.44 Release),
seven alternating pairs gave median times of 867.4 ms for the default
baseline and 862.2 ms after disabling observation. Comparing the existing M11
CLI with the M12 CLI on the same host over seven alternating 600-frame pairs
gave medians of 2,146.8 ms and 2,205.0 ms respectively, a 2.7% increase.
Both CLIs returned identical JSON for the frozen AV fixture. The CLI comparison
includes process startup and measures the combined changes in this milestone.
Raw timings are in `build/m12/disabled-observation-ms.csv` and
`build/m12/m11-m12-cli-ms.csv`.

CTest passed on Windows Debug and Release with desktop enabled (19 each), and
on Ubuntu 24.04 WSL Release with desktop enabled (19) and headless Debug (18).
The timing check measures headless execution on one development host.
