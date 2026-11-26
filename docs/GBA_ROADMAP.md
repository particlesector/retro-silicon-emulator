# Game Boy Advance roadmap

The GBA has two planned release goals. The only implemented built-in system at the M20 baseline is `nes-ntsc`.

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

- [ ] A project-owned program boots on a test bus, branches from ARM to Thumb and back, writes a hand-calculated RAM signature and reaches the expected final PC. The core builds independently of the GBA system.
- [ ] ARM and Thumb instruction cases compare registers, flags and memory against independent expectations. Cover arithmetic and shift carry, conditional branches, pipeline-visible PC, aligned and unaligned transfers, and bus access widths.
- [ ] Exception tests check the software interrupt and IRQ vectors, banked registers, saved status and return PC. Running the same fixture in one budget and in smaller chunks yields the same state and ordered bus transactions.

### M22 · GBA memory map and BIOS boot

- [ ] Headless tests cover BIOS read protection, internal and external work RAM, video memory and cartridge ROM. Compare data values, access widths and elapsed ticks for representative sequential and nonsequential Game Pak accesses.
- [ ] An authored boot fixture runs in CI. A missing or wrong-sized BIOS and a truncated cartridge produce actionable loader errors and preserve any active session.
- [ ] In a private run, start at the BIOS reset vector with the identified LeafGreen cartridge. Record the first instruction fetch from cartridge entry at `0x08000000` and the elapsed tick. A second cold boot reaches the same boundary. Supply the LCD status, DMA and interrupt behavior required by that boot path.

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
