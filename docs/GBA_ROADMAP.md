# Game Boy Advance roadmap

**Target:** Play the original US Pokémon LeafGreen on the Windows and Linux desktop, with working controls, graphics, audio and durable in-game saves. M21–M26 are planned. The only implemented built-in system at the M20 baseline is `nes-ntsc`.

## Media and test inputs

| Local input | Identity | Use |
| --- | --- | --- |
| `Pokemon - LeafGreen Version (USA).gba` | 16 MiB, game code `BPGE`, revision 0, SHA-1 `574fa542ffebb14be69902d1d36f1ec0a4afd71e` | Private gameplay acceptance. The image contains `FLASH1M_V103`, identifying 128 KiB of banked flash. |
| `gba_bios.bin` or `boot.rom` | Both local files are 16 KiB with SHA-1 `300c20df6731a33952ded8c436f7f186d25d3492` | Supply either file as the BIOS. Boot through its reset vector and execute its software-interrupt handlers. |

The inspected files are in `C:\roms\GBA` on the development machine. Accept user-selected paths on both platforms. Keep firmware, retail cartridges and personal saves external to the repository, CI and packages. Use assembled project-owned ROMs for regression tests. Record license, revision and expected results when adopting external test cases. [LeafGreen identities](https://github.com/pret/pokefirered) · [BIOS checksums](https://wiki.ds-homebrew.com/gbarunner2/bios) · [mGBA hardware tests](https://github.com/mgba-emu/suite)

## Implementation and acceptance

Each milestone depends on the preceding one and may span several PRs. Before implementing a case, define its input, expected result and observation point. Close each checkbox with a test name or a private-run report identifying the build, platform, media digests and observed result. Apply the [project completion rules](ROADMAP.md#how-milestones-close) throughout.

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

## Engineering boundaries

Keep the ARM core independent of GBA address decoding. The GBA machine owns clocks, bus wait states, IRQ routing and DMA ownership. Return completed frames, PCM and named inputs through the [session contract](SESSIONS.md). Extend the built-in loader to take firmware and save configuration while preserving the existing NES entry points. Host file access and SDL/Vulkan calls belong outside the chip models. [GBA hardware overview](https://gbadev.net/tonc/hardware.html)

The first Gym is the proposed release checkpoint. A full campaign through the Elite Four and postgame needs a separately recorded playthrough. Link-cable trading, wireless accessories, other cartridge save types, GBA snapshots and AI training can follow evidence of demand. Check off a milestone only after its acceptance results are recorded in this document.
