# NES snapshots and replay

`Session::snapshot()` returns an owned byte vector. `Session::restore(bytes)`
accepts snapshots from the same supported machine, cartridge and deterministic
profile. The NES adapter implements both operations. A session without this
capability throws `UnsupportedOperation`. The serialized C++ automation API
and optional Python session expose the same calls. The caller may store the
bytes in a binary file and load them into another matching instance.

## State and output cursors

The snapshot captures the committed master tick, CPU and PPU clock edges,
interrupt lines, bus latch, CPU registers and pending micro-operation, PPU
pipeline and both frame buffers, APU sequencer and channel state, OAM/DMC DMA,
RAM, writable CHR, controller shift state, pending events in insertion order,
and PCM integration and filter state. The NTSC NROM adapter has no RNG or
switchable mapper bank registers.

The 6502 stores a bounded transcript of bus results and interrupt state for
the active instruction or reset/interrupt entry sequence. Restore rebuilds the
coroutine in a new core using only that transcript. Held DMA slots do not
advance the coroutine. The scheduler, DMA state and CPU cycle counter preserve
their timing. Rebuilding the coroutine performs no live bus transactions.

Restore constructs and validates a replacement machine before committing it.
On success the latest published frame is available at its saved frame number,
including when a subsequent frame was in progress. Pending PCM and trace
records are cleared, and active observation subscriptions are discarded.
The next PCM samples use the restored integration phase and filter history.
The caller should flush any samples or frames already copied to host queues,
then prime those queues from the restored session. Automation pause state is a
host control and stays with its owner. Faulted machines can be recovered by
restoring a valid snapshot. Snapshot capture requires a healthy machine.

## Portable format and action logs

Version 1 uses fixed-width little-endian fields with explicit encodings for
booleans and optionals. The file envelope contains a four-byte type tag, a
16-bit version, 16 reserved zero bits, 64-bit cartridge and configuration
identities, a 32-bit payload length, and a 64-bit payload integrity hash.
The type tags are `RESS` for snapshots and `RESL` for action logs. The hashes
are FNV-1a checks for compatibility and accidental corruption. The cartridge
identity covers accepted PRG and immutable CHR bytes, mirroring and CHR type.
The configuration identity covers CPU/PPU phases, DMA parity and PCM capture.
Trace and host queue limits are properties of the receiving session.

Payloads are bounded to 12,000,000 bytes and at most 1,000,000 scheduled
actions. Restore validates the envelope, consumed length, clock consistency
and chip field bounds. Unsupported versions and incompatible files fail
before replacing the destination.

`nes::Machine::encode_replay(target, actions)` records a power-on replay.
Its payload starts with the inclusive 64-bit end tick and 32-bit action count.
Each action contains a 64-bit tick, one-byte kind and one-byte value. The
kind is 0 for a controller button mask or 1 for an external IRQ level.
Actions use strictly positive timestamps ordered by tick. Equal-time records
preserve insertion order. Controller bits use the NES adapter's standard A,
B, Select, Start, Up, Down, Left and Right order, starting at bit zero.

`load_replay(bytes)` creates a fresh power-on timeline and returns the end
tick. Advance toward it in any chunk sizes, draining PCM as needed. A failure
log uses a target at or after the failing operation. The replay test checks
that a project-owned NROM program reproduces the same unsupported opcode,
address and trace event across different chunk sizes.
Run `rse-snapshot-tests <output-directory>` to export the failing ROM and
its portable action log, along with a regular snapshot and action log for
the project-owned loop fixture.

## Validation

Windows MSVC 19.44 Debug and Release passed all 22 CTest suites with desktop
and Python enabled. Snapshot tests compare canonical state bytes, trace
ordering, frames and PCM across chunk sizes, including restores during CPU
reset, an instruction, NMI entry, OAM/DMC DMA, rendering and after publication.
The pinned version 1 fixture hashes matched on Windows and Ubuntu. Linux CI
passed headless Debug, headless Release with Python, ASan/UBSan, and the
packaged desktop smoke tests. Local WSL failed to start, so Linux verification
used the [GitHub Actions run](https://github.com/particlesector/retro-silicon-emulator/actions/runs/44462148902).

On the Windows development host, five alternating M13/M14 CLI pairs ran 300
frames of the frozen AV fixture with identical JSON. Median elapsed times
were 1,116.6 ms and 1,109.5 ms respectively. Raw readings are in
`build/m14/m13-m14-cli-ms.csv`. This test measures headless execution.
