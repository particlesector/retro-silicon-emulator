# Parallel headless sessions

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

## Windows development-host results

Windows MSVC 19.44 Release on the Ryzen AI Max+ 395 ran 90 steps per slot.
Each parallel run produced the same aggregate canonical hash as the
single-worker run for the same environment count. All reported frames/s also
equal slot steps/s for this fixture and interval.

| Environments | Workers | Serial frames/s | Parallel frames/s | Million ticks/s | CPU % | Peak MiB | Obs. MiB/s |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 1 | 292 | 292 | 105 | 96 | 5.3 | 34 |
| 2 | 2 | 293 | 489 | 175 | 182 | 5.9 | 57 |
| 4 | 4 | 289 | 987 | 353 | 351 | 6.9 | 116 |
| 8 | 8 | 290 | 1,337 | 478 | 676 | 9.0 | 157 |
| 16 | 16 | 287 | 2,088 | 746 | 1,466 | 12.7 | 245 |
| 32 | 32 | 290 | 2,513 | 898 | 2,212 | 20.5 | 295 |

With 32 fixed environments, 2/4/8/16/32 workers delivered approximately
546/1,041/1,533/2,022/2,513 frames/s. The initial throughput choice for this
host and workload is 32 workers, about 8.7 times the serial rate. A 16-worker
pool delivered about 80% of that throughput with lower aggregate CPU use.
Raw serial and parallel results are in `build/m15/parallel-benchmark.csv`.
Three additional 180-step pairs at 32 environments gave medians of
2,042 frames/s with 16 workers and 2,483 frames/s with 32 workers. All six
runs produced aggregate hash `08d82cd24f67ca9d0`.

Windows Debug and Release passed all 23 suites with desktop and Python
enabled. Ubuntu validation is pending because the local WSL VM timed out
before configuration began.
