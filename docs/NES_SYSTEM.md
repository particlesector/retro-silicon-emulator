# NES system integration (M5)

## Clock, ownership, and access contract

The NES machine owns chip wiring; the 6502, 2C02, controller shift register,
and OAM DMA remain separate components. A machine tick is one NTSC master-clock
period. PPU clocks occur every four ticks, CPU slots every twelve. Default phase
zero clocks first at ticks 4/12; configurable phases select other integer offsets.
This fixes reproducibility without claiming every physical half-cycle alignment.

Advance processes (current, target], including events exactly at the target.
At a shared tick, apply scheduled input/IRQ events in insertion order, clock the
PPU, route its NMI level and publish frame observations, then clock the CPU/DMA
slot. CPU register side effects update the NMI wire before CPU pin sampling.
Odd-frame PPU counter skips do not change this oscillator relationship. Runs can
stop between device edges and must remain invariant under advancement chunking.

The reusable CPU bus can deny a read grant while another bus owner performs its
cycle. CPU clocks and interrupt-pin sampling continue, but the pending read is
not consumed. Writes cannot be halted. This hook is not NES address decoding.
OAM DMA halts the next CPU read after $4014, optionally aligns to a get phase,
then performs 256 get/put pairs. All accesses use the ordinary mapped bus, including
halt/alignment repeats and $2004 writes. Consecutive $4014 writes select the last
page before the halt. Get/put power-on phase is a reproducible profile option.
DMC arbitration is M6 work.

The standard controller uses a separate CD4021 model. Button bits are A, B,
Select, Start, Up, Down, Left, Right from bit 0 through bit 7. Strobe-high loads
continuously, strobe-low freezes the sample for serial reads, and exhausted reads
return one at the CPU port. Physical button pins are active-low and the 4021
serial input is grounded; the console performs DATA inversion. Host events carry
explicit future master ticks; equal-tick events
apply in stable insertion order. External IRQ events exercise the wire without
pretending NROM has an IRQ source; APU/mapper IRQ sources arrive separately.

## CPU memory and observation

CPU RAM and NROM retain the M2 map. $2000-$3FFF mirrors native PPU registers;
$4014 requests OAM DMA and $4016 strobes/reads controller 1. Unconnected controller
port 2 drives zero on its low lines. Unmapped reads retain the CPU bus latch;
controller reads use NES-001 wiring: bits 0-4 driven, latched high bits retained.
NES-101/Famicom port differences are not implied. APU registers are
unconnected until M6. The external PPU bus still owns cartridge/nametable memory;
palette and OAM remain inside the PPU.

Inspection of CPU RAM/PRG and chip diagnostic state is non-mutating. It must not
read device ports. Optional bounded traces record master ticks, source-domain
clock numbers, access kind, address, and data in deterministic order. These are
diagnostic records, not a frozen trace-file ABI or serialized machine state.
On execution/trace errors, the machine faults; partial work is not rolled back.
Reset reconstructs a deterministic power-on timeline for the same cartridge and
profile, clears queues/traces/RAM/chip state, and releases controller buttons.
It is not a hardware warm-reset pin model. Existing M4 PPU limits still apply.

## Reference plan and acceptance cases

References: NESdev [DMA](https://www.nesdev.org/wiki/DMA),
[standard controller](https://www.nesdev.org/wiki/Standard_controller),
[controller ports](https://www.nesdev.org/wiki/Controller_reading), and
[PPU clock relationship](https://www.nesdev.org/wiki/PPU).
The logical 4021 behavior follows the [TI CD4021B specification](https://www.ti.com/product/CD4021B).
Use project-owned generated NROM programs and fixed expected values, not game ROMs.

- Chip cases: 4021 parallel load/serial input and controller button order, held
  strobe, falling-edge capture, changing live buttons, and reads beyond eight bits.
- CPU arbitration: denied read repeats without register/PC advancement, continuing
  pin sampling and timestamps, and rejection of attempted write halts. Existing
  instruction reference suites must remain unchanged and pass.
- DMA cases: both 513/514-slot alignments, all 256 source reads and OAM writes,
  OAM wrapping, write-delayed halt/last-page selection, side-effecting halt reads,
  continued PPU time, and NMI latched while CPU execution is held.
- System cases: clock/phase counts, register mirrors, open bus, timestamped input,
  NMI/IRQ priority and delivery, reset/failure behavior, and runs stopped mid-slot.
- Fixed-duration programs: repeat and chunk identical input timelines; compare
  CPU/PPU/RAM observations, ordered event hashes, and frame hashes. Pin expectations
  independently from simple loop arithmetic and explicitly assembled fixture code.

## Deterministic fixtures

`nes-system-tests` runs a generated NROM loop for 720,089 master ticks. An
independent oracle constructs its reset sequence and 7,500 complete INC/JMP
iterations: 60,007 CPU slots, RAM $10 = $4C, PC = $8000, and two published frames.
FNV-1a hashes encode each diagnostic numeric field as eight little-endian bytes;
the frame hash consumes palette samples. Pinned expectations are:

| Observation | Hash |
| --- | --- |
| Ordered events | `fc34a569a9f0d2eb` |
| Machine state | `69f1cb6cc7e11967` |
| Blank frame | `cf28946e7ff82325` |

A second generated program configures CHR RAM and palette output, then samples
scheduled controller input in NMI handlers. It checks captured values $A5/$3C,
eight serial reads per sample, and a uniform $21 frame. Repeated, reset, and
chunked runs compare full observations. Fixtures are project-owned source code;
these checks establish the listed behavior, not broad game compatibility.

## Headless runner

After building, run a supported mapper-0 iNES image for a bounded duration:

```sh
rse-cli run example.nes --ticks 720089
rse-cli run example.nes --frames 2
```

On Windows, the executable is in `build/<configuration>/Release` for a
multi-configuration build; use the build directory chosen in the README.
Counts are unsigned decimal 64-bit integers; zero is valid. Frame runs stop at
actual frame publication. No video window or audio output is produced.

Success prints one JSON record with format `rse.run.v1`, elapsed master `ticks`,
`cpu_cycles` (including DMA holds), published `frames`, and decimal `pc`.
Exit codes: 0 success, 2 arguments, 3 unsupported/malformed cartridge, 4 execution
failure, 5 cartridge I/O failure, 6 other loading failure. Errors go to stderr.
Unsupported CPU opcodes fail explicitly; APU/audio and desktop integration remain
M6/M7 work.
