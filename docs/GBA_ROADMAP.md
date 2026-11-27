# Game Boy Advance roadmap

The GBA has two planned release goals. The `gba` built-in supports headless and desktop playback with video, keypad input and stereo audio. The default system is `nes-ntsc`.

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

- [x] `gba-keypad` checks 240×160 video, 3:2 aspect and ten named actions. An authored ARM fixture samples KEYINPUT on fixed ticks and compares every pressed and released bit, including a three-button chord. It also checks equal-tick precedence, reset, and keypad IRQ conditions.
- [x] `gba-timing` compares an ordered trace at tick 197,120. VBlank entry and frame publication precede the Timer 0 overflow and Timer 1 cascade, then DMA 0 transfers before DMA 1. It checks IF acknowledgement, VBlank exit at tick 279,664 and DMA preemption at a transfer boundary. `gba-frame-replay` checks that consecutive publications are 280,896 ticks apart. [LCD timing](https://gbadev.net/gbadoc/graphics.html)
- [x] `gba-scenes` checks bitmap pages, 4bpp and 8bpp text tiles, affine wrapping, regular and affine sprites, overlap priority, rectangular and OBJ windows, alpha and brightness, and a palette update by HBlank DMA. `gba-frame-replay` checks that scheduled shoulder presses change pixel zero on frame 2 and release restores it on frame 3. Whole-frame and uneven advances produce the same BGR555 FNV-1a hashes `cb8b539a481cde5f`, `6941e3232f2803da`, and `cb8b539a481cde5f`.

The tested implementation is `57ab72d`. Headless CTest passed 30/30 in Debug and Release on Windows x64 with MSVC 19.44 and Ubuntu 24.04 WSL with GCC 13.3. Release builds on both platforms repeated the M22 private cold boots at tick 75,690,221.

The renderer composes one line at HBlank and publishes the completed image at VBlank entry. Mid-scanline register effects, OBJ evaluation cycle limits and physical CPU or video-memory contention still need hardware validation. [GBA hardware reference](https://mgba-emu.github.io/gbatek/)

### M24 · Desktop play and sound

- [x] `gba-audio` checks each PSG generator, left/right routing and timer selection for the two Direct Sound channels. Its first FIFO A request is at tick 612, and the four DMA words complete at tick 622. An authored program generates 548 stereo frames by tick 280,896. Its interleaved S16 FNV-1a hash `7bde98df7a93717d` matches after reset and uneven advances.
- [x] The authored GBA desktop smoke loads external ROM and BIOS files and checks the 240×160, 3:2 frame through Vulkan readback. It verifies all ten keyboard and all ten standard gamepad bindings against the ROM's KEYINPUT display. Both extracted Release packages pause, resume and reset correctly, and report zero starvation requests over six seconds with SDL dummy audio.
- [x] A private LeafGreen run reached the title, started a new game, selected Bulbasaur, defeated rival B's Charmander with Tackle, and returned to Oak's lab. Captures verified title and dialogue text, trainer and Pokémon sprites, battle HUD, victory text, and transitions into and out of battle. PCM captures verified title music, battle audio and an input-triggered selection effect.

The tested implementation is `5136e9b`. CTest passed 33/33 with desktop enabled in Debug and Release on Windows x64 with MSVC 19.44 and Ubuntu 24.04 WSL with GCC 13.3. The independent Windows headless Release build passed 31/31. Extracted Windows and Linux desktop smokes each verified 20 bindings, two Vulkan readbacks and one reset. A 21-second Windows Release desktop run with private media also reported zero starvation requests. Repeat private cold boots fetched cartridge entry on tick 75,690,241 with the sound model active.

The private input run used the BIOS and LeafGreen USA revision 0 SHA-1 values in the media table, on Ubuntu 24.04 WSL with GCC 13.3 and optimization enabled. The frame numbers below count from the first publication after cold reset.

| Frame | Observed checkpoint and input path |
| --- | --- |
| 1,091 | LeafGreen title. Pressed Start and acknowledged the absent flash-board notice with A. |
| 5,298 | Player A's bedroom. Advanced the instructions and Oak's introduction with A, chose the boy character, and entered A for the player and B for the rival using the D-pad, A and Start. |
| 5,900 | Pallet Town. Walked right, up and left to descend the stairs, crossed the living room to the front door, and exited south. Walked east, then north to the grass to trigger Oak's lab escort. |
| 9,689 | Received Bulbasaur. Advanced Oak's dialogue, approached the left ball from below, confirmed with A, and declined the nickname. Walked toward the lab exit to trigger B's challenge. |
| 11,399 | Battle introduction. Advanced the challenge and battle tutorial with A, then selected Fight and Tackle. |
| 16,297 | Victory. Bulbasaur reached level 6 with 9/21 HP, and the game displayed the rival defeat message. |
| 18,005 | Returned to the lab after advancing the result and tutorial text. |

The title PCM contained 65,272 nonzero channel samples over 32,917 stereo frames. A control replay of the naming screen matched the preceding video frame's PCM byte-for-byte and differed by 6,982 channel samples after pressing A to enter a character. The audio checks use captured digital PCM. [Title music call](https://github.com/pret/pokefirered/blob/master/src/title_screen.c) · [Naming effect call](https://github.com/pret/pokefirered/blob/master/src/naming_screen.c)

The mixer produces fixed-rate 32,768 Hz stereo PCM. PWM resolution changes and analog output characteristics remain unverified. Flash detection and persistence are tracked in M25. [GBA sound registers](https://mgba-emu.github.io/gbatek/#gba-sound-controller)

### M25 · Flash saves and recovery

- [x] `gba-flash-commands` checks Sanyo ID `0x1362`, unlock and program commands, bit clearing, sector endpoints, chip erase, and independent 64 KiB banks. It verifies that machine reset preserves flash contents and clears the command and bank selection state.
- [x] `gba-flash-files` checks independent cartridge paths, dirty data across reset, and two successive raw 128 KiB saves. On Windows, denying replacement of the previous save reports an error and leaves the old file byte-identical. On Linux, a directory at the destination triggers a rename error. Retrying after removing the obstruction commits the pending image. Empty, oversized and undersized saves are rejected and preserved.
- [x] A private LeafGreen run saved inside Oak's lab and then outside the lab in Pallet Town. Two cold launches restored the trainer, party and the location from the corresponding save.

CTest passed 35/35 with the desktop enabled in Debug and Release on Windows x64 (MSVC 19.44) and Ubuntu 24.04 WSL (GCC 13.3). The independent Linux headless Release build passed 33/33. An authored flash-writing cartridge exercised both extracted Release desktop packages. Each package reopened an existing 131,072-byte save and changed the programmed byte from `0x3f` to `0x0f` across boot and reset. Both six-second smoke runs verified 20 input bindings and two Vulkan readbacks with zero starvation requests. Two successful CLI processes independently exercised first flush and reload on Windows.

The private run used the M25 Release built-in session libraries on Ubuntu 24.04 WSL with GCC 13.3 and the BIOS and LeafGreen USA revision 0 SHA-1 values in the media table. A separate process for each pass replayed keypad input at frame boundaries, captured the game's screens, and flushed on normal exit. Frame counts restart at each cold boot.

| Pass | Observed checkpoint |
| --- | --- |
| First save | At frame 23,149, the game confirmed trainer AAA's save inside Oak's lab. The party held one male Bulbasaur at level 5 with 20/20 HP. |
| First Continue and second save | Continue displayed AAA and play time 0:04 at frame 1,373. After the recap, frame 2,076 showed AAA next to the starter table and frame 2,250 confirmed the same party. After the rival battle, AAA left the lab and confirmed overwriting the in-game save at frame 11,448. |
| Second Continue | Continue displayed AAA and play time 0:07 at frame 1,373. Frame 2,027 restored AAA outside the lab entrance in Pallet Town. Frame 2,221 confirmed the same Bulbasaur and empty remaining party slots. |

Both exit flushes produced 131,072-byte files with different SHA-256 digests. The final cold verification left the second save byte-identical.

The chip completes program and erase operations immediately. The board models the byte-wide backup window and WAITCNT bus waits. The host stages complete images before replacement and commits dirty storage on orderly exit or cartridge replacement. [Flash commands and backup bus](https://mgba-emu.github.io/gbatek/#gba-cart-backup-flash-rom)

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
