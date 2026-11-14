# NES CPU reference vectors

`vectors.txt` is a numeric transcription of the first 64 cases per documented
opcode from [SingleStepTests/65x02](https://github.com/SingleStepTests/65x02),
`nes6502/v1`, pinned to commit `2f6980a2d95757486c7bee24355c360e40e2a224`.
There are 151 opcodes and 9,664 cases. Original test data is MIT-licensed;
see [LICENSE](LICENSE). No game ROM or emulator implementation is included.

Regenerate from the repository root with PowerShell:

```powershell
./tools/import_cpu_vectors.ps1 -Processor nes6502
```

Builds and tests run offline. Each case contains an identifier, initial and final
registers (`pc s a x y p`) plus RAM pairs, then ordered bus cycles
(`address value writing`, with writing 0 or 1). The header names the format and
case count. Cycle timestamps are implicit consecutive CPU clocks starting at 1.
Each record occupies one line; the reader treats whitespace uniformly.
The importer changes representation only; it does not compute expected behavior.

This subset is an instruction/bus regression oracle, not complete hardware or
interrupt-line verification. Interrupt/reset cases use separate synthetic tests.

SHA-256 (LF file): `10e323f53d25a3e6f8deaa1835630b3da883ad8ead9a5e11f5503f703db4aa75`.
