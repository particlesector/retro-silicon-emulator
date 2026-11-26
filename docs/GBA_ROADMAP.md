# Game Boy Advance roadmap

The GBA has two planned release goals. The `gba` built-in currently supports headless BIOS boot, and `nes-ntsc` remains the default.

1. **Single-player LeafGreen (M21–M26).** A player earns the Boulder Badge on the Windows and Linux desktop and can continue from the in-game save after relaunch.
2. **Two-player LAN trading (M27–M30).** Two people on separate computers trade through the games’ cable trading room, one playing FireRed and one playing LeafGreen. Each keeps the received Pokémon in their own save after both emulators restart.

## Media and test inputs

| Local input | Identity | Use |
| --- | --- | --- |
| `Pokemon - LeafGreen Version (USA).gba` | 16 MiB, game code `BPGE`, revision 0, SHA-1 `574fa542ffebb14be69902d1d36f1ec0a4afd71e` | Private gameplay acceptance. The image contains `FLASH1M_V103`, identifying 128 KiB of banked flash. |
| `Pokemon - FireRed Version (USA).gba` | 16 MiB, game code `BPRE`, revision 0, SHA-1 `41cb23d8dccc8ebd7c649cd8fbb58eeace6e2fdc` | Private LAN trading partner for LeafGreen. Each player supplies their own cartridge image, BIOS and save. |
| `gba_bios.bin` or `boot.rom` | Both local files are 16 KiB with SHA-1 `300c20df6731a33952ded8c436f7f186d25d3492` | Supply either file as the BIOS. Boot through its reset vector and execute its software-interrupt handlers. |

The inspected files are in `C:\roms\GBA` on the development machine. Accept user-selected paths on both platforms. Keep firmware, retail cartridges and personal saves external to the repository, CI and packages. Use assembled project-owned ROMs for regression tests. Record license, revision and expected results when adopting external test cases. [LeafGreen identities](https://github.com/pret/pokefirered) · [BIOS checksums](https://wiki.ds-homebrew.com/gbarunner2/bios) · [mGBA hardware tests](https://github.com/mgba-emu/suite)

## Milestone acceptance

Each milestone depends on the preceding one and may span several PRs. Before implementing a case, define its input, expected result and observation point. Close each checkbox with a test name or a private-run report identifying the build, platform, media digests and observed result. Apply the [project completion rules](ROADMAP.md#how-milestones-close) throughout.

## Level 1 · Single-player LeafGreen

### M21 · ARM7TDMI execution

- [x] The project-owned `interworking` fixture boots on an independent test bus, branches from ARM to Thumb and back, writes the word `0x0000005A` at `0x84`, and reaches ARM PC `0x44`. It checks every logical fetch and store.
- [x] ARM and Thumb cases check registers, flags, PC and RAM against hand-calculated results. `arm_flags_and_branches`, `thumb_flags_and_branches` and `register_shift_edges` exercise arithmetic, shift carry, conditional branches and pipeline-visible PC. `transfer_widths` checks byte, halfword and word transactions, including the aligned bus address and rotated result of an unaligned word load in each instruction set.
- [x] `arm_exception_boundaries` and `thumb_exception_boundaries` check SWI at `0x08`, IRQ at `0x18`, banked SP/LR, saved status and return PC. A held IRQ crosses a masked SWI handler and is taken before the resumed Thumb fetch. The same fixture run in one 20-step budget, at handler boundaries, and in uneven chunks matches across CPU state (including dormant banks), RAM and the complete ordered bus trace.

All cases run in [arm7tdmi-tests](../tests/arm7tdmi_tests.cpp). Headless CTest passed 23/23 on Windows MSVC in Debug and Release and on Ubuntu 24.04 WSL with GCC 13.3 in Release.

The [CPU interface](../include/rse/cpu/arm7tdmi.hpp) counts instruction boundaries and IRQ entries. Bus observations cover logical accesses. Physical prefetch and refill cycles, wait states and BIOS execution need system-level validation. The tested subset covers User/System, Supervisor and IRQ banks. Additional instructions, modes and odd-address halfword behavior need separate coverage as the boot path exercises them. Instruction and exception expectations follow sections 3–5 of the [ARM7TDMI Data Sheet, DDI 0029E](https://blogs.igalia.com/aboya/files/2025/08/ARM7TDMI-Manual-1995.pdf).

### M22 · GBA memory map and BIOS boot

- [x] `gba-memory-map` covers BIOS read protection, internal and external work RAM, palette RAM, VRAM, OAM and cartridge ROM. It checks Game Pak byte, halfword and word values and elapsed ticks across sequential and nonsequential accesses, all three ROM windows, a 128 KiB boundary and a WAITCNT change.
- [x] `gba-authored-boot` runs through CTest in CI using generated firmware and cartridge bytes. It checks reset, the cartridge entry fetch, a BIOS SWI and return, and matching entry observations after reset and different advance sizes. Missing or wrong-sized BIOS files and a cartridge shorter than its header produce actionable diagnostics and preserve the active session.
- [x] Two independent private cold boots per tested configuration started at the BIOS reset vector and reached the first ARM fetch at `0x08000000` on tick **75,690,221**, reading four bytes as opcode `0xEA00007F`. `gba-lcd-dma-irq` checks LCD status transitions, HALT wake, immediate and VBlank DMA, DMA completion IRQ and byte-wise interrupt acknowledgement.

The tested implementation is `4fad45c`. Headless CTest passed 26/26 on Windows x64 with MSVC 19.44 and Ubuntu 24.04 WSL with GCC 13.3, both in Debug and Release. The private boot pair matched in Windows Debug, Windows Release and Ubuntu WSL Release. The 16 KiB BIOS SHA-1 was `300c20df6731a33952ded8c436f7f186d25d3492`. The 16 MiB LeafGreen USA revision 0 SHA-1 was `574fa542ffebb14be69902d1d36f1ec0a4afd71e`.

Run the private check from the headless build with `rse-gba-boot-tests private "<LeafGreen ROM path>" "<BIOS path>"`. For a bounded headless CLI run, use `rse-cli run "<ROM path>" --ticks <count> --system gba --bios "<BIOS path>"`.

The recorded tick is for the start of the logical fetch in this model. Full ARM pipeline and internal cycle timing, Game Pak prefetch and LCD memory contention still need hardware validation. The LCD logic at this milestone supplies timing and status, and the DMA and interrupt logic cover the boot path. [GBA hardware reference](https://mgba-emu.github.io/gbatek/) · [ARM7TDMI technical reference](https://documentation-service.arm.com/static/5e8e1323fd977155116a3129)

### M23 · Display, timing and keypad

- [ ] The headless `gba` session advertises 240×160 video, 3:2 aspect and ten named buttons. A fixture checks the pressed and released state of every button, including simultaneous presses, at specified ticks.
- [ ] Timing fixtures check VBlank entry and exit, timer overflow, IRQ acknowledgement and DMA arbitration. At coincident events, compare the ordered trace against a specified expectation. Consecutive frame publications are 280,896 system clocks apart. [LCD timing](https://gbadev.net/gbadoc/graphics.html)
- [ ] Authored bitmap, tile and sprite scenes check expected pixels, including overlap priority, windows and blending used by the target. A scheduled button press changes a known pixel pattern on the expected frame. Different advance sizes produce identical frame hashes.

### M24 · Desktop play and sound

- [ ] A generated sound fixture exercises PSG channels and timer/DMA-fed Direct Sound. Check FIFO refill timing, left/right routing and sample counts. Compare stereo PCM hashes across reset and chunked runs.
- [ ] Desktop smoke loads a user-selected ROM and BIOS, presents the session dimensions at 3:2 aspect and verifies all ten keyboard/gamepad bindings, including L/R. Pause/resume and reset recover input, frames and audio. The bounded playback run reports zero audio starvation.
- [ ] In a private LeafGreen run, reach the title, start a new game, choose a starter and finish the first rival battle. Record the input path and check visible text, sprites, battle transitions, music and sound effects.

### M25 · Flash saves and recovery

- [ ] Flash tests check chip identification, program and erase commands, sector boundaries and both banks of the 128 KiB device. Writing one bank preserves the other. Machine reset preserves flash contents.
- [ ] The application uses a per-cartridge save path and atomically replaces the last successful save on flush. A failed replacement leaves the previous file byte-identical and reports the error. A malformed save size produces an error and preserves the original file.
- [ ] Save from the LeafGreen menu, exit the process, relaunch and choose Continue. Verify trainer, party and location. Move to a different location, save again and repeat the cold launch to verify the newer progress.

### M26 · Packaged gameplay acceptance

- [ ] Extract Release packages on Windows and Linux. On each platform, load external media, start with a fresh save and play through indoor/outdoor transitions, menus and multiple battles. Record session duration and verify zero audio starvation during a ten-minute uninterrupted gameplay interval.
- [ ] On each platform, defeat the first Gym Leader, save, close the application and relaunch. Continue must restore the Boulder Badge, party and saved location. Record the observed results and any visual, input, audio or save defects.
- [ ] Run the GBA headless fixtures and frozen NES regressions on both platforms and compare deterministic outputs. Publish the tested build, configurations, media digests, gameplay checkpoints and known limitations. A defect preventing any listed checkpoint keeps the milestone open.

## Level 2 · Two-player LAN trading

This work begins after M26. Keep serial registers, interrupts and transfer timing in the GBA model. Connect two headless machines locally first, then carry the same cable exchanges between desktop processes. The host application owns the sockets and connection UI. Each machine owns its input and flash save.

### M27 · Emulated two-player cable

- [ ] Two project-owned headless ROM fixtures select multiplayer serial mode, take player IDs 0 and 1, and send distinct 16-bit words. Both machines read the expected words in the corresponding receive registers. [GBA serial registers](https://gbadev.net/gbadoc/registers.html)
- [ ] A fixture starts consecutive transfers and checks busy status, completion tick and serial interrupt behavior with interrupts enabled and disabled. Pin the expected register values and event order before running the test.
- [ ] Run the fixture pair with different execution chunk sizes and alternate which machine advances first. Each endpoint produces the same ordered transfer trace and final state as its reference run.

### M28 · LAN transport and synchronization

- [ ] Run the M27 fixture in two processes over loopback. Both endpoints reproduce their local-cable words, order and emulated completion ticks. The protocol carries link events and session metadata. ROM, BIOS and save files stay on their owner's machine.
- [ ] Repeat with a documented schedule of delayed and fragmented network delivery. Compare each endpoint's trace with M27 and verify every transfer completes exactly once. Waiting for a peer may extend wall-clock time.
- [ ] Reject incompatible protocol versions with a visible reason. Interrupt an active transfer and verify both processes report disconnection within a documented timeout. A fresh connection starts with empty transfer queues and completes the fixture.

### M29 · Desktop hosting, joining and recovery

- [ ] On two computers on the same LAN, one player hosts and the other joins by address. Each desktop shows connection status, accepts its player's controls and lets that player leave. Repeat with Windows and Linux exchanging host roles.
- [ ] In a private run, load independent trade-ready saves and enter the cable trading room in each game. Both players see the other trainer and can reach the trade confirmation screen. Cancel a trade and verify that both parties retain their offered Pokémon.
- [ ] Using disposable copies, interrupt the LAN connection once before confirmation and once during the exchange. Reopen both games and verify each can recover its last good save. Record which Pokémon each save contains at each interruption point. Reconnect and complete a subsequent exchange.

### M30 · Packaged FireRed and LeafGreen trade

- [ ] Two human testers use extracted Windows and Linux Release packages on separate computers. Record the builds, cartridge digests, host/join roles, and the species, nickname and original trainer of each offered Pokémon. Use distinguishable Pokémon that do not evolve in a trade.
- [ ] Complete the exchange through the in-game cable trading room. Each player verifies the received Pokémon's identity, saves, closes the process, relaunches independently and chooses Continue. Both saves must contain the received Pokémon and each offered individual must be absent from its original save.
- [ ] Swap host/join roles and trade the pair back. Repeat the cold-launch checks on both machines. Publish the results alongside the passing M27–M29 fixture and recovery checks and the single-player regression results. Keep personal saves and retail media private.

## Engineering boundaries

Keep the ARM core independent of GBA address decoding. The GBA machine owns clocks, bus wait states, IRQ routing and DMA ownership. Return completed frames, PCM and named inputs through the [session contract](SESSIONS.md). Extend the built-in loader to take firmware and save configuration while preserving the existing NES entry points. Host file access and SDL/Vulkan calls belong outside the chip models. [GBA hardware overview](https://gbadev.net/tonc/hardware.html)

The second checkpoint covers two-player cable trading over a LAN. Link battles can follow with their own gameplay test. A full campaign, wireless accessories, other cartridge save types, GBA snapshots and AI training remain future work. Check off a milestone only after its acceptance results are recorded in this document.
