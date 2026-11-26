# Development roadmap

The NTSC NES and AI-enabled MVPs are complete. The next system is the Game Boy Advance, with Pokémon LeafGreen as the first private gameplay target. [GBA implementation and acceptance milestones](GBA_ROADMAP.md)

| Completed | Delivered | Evidence |
| --- | --- | --- |
| M1–M8 | Deterministic NTSC NROM execution, 6502, PPU, APU, input, DMA, headless CLI and packaged SDL3/Vulkan desktop | [Frozen fixture manifest](../tests/acceptance/manifest.json) · [M8 acceptance](https://github.com/particlesector/retro-silicon-emulator/blob/c592dc7857da82cfceceda64d1fc0e27ded713e0/docs/MVP_ACCEPTANCE.md) |
| M9–M11 | Common machine sessions and metadata-driven CLI and desktop playback | [Session migration and playback validation](https://github.com/particlesector/retro-silicon-emulator/blob/c592dc7857da82cfceceda64d1fc0e27ded713e0/docs/M11_VALIDATION.md) |
| M12–M20 | Inspection, snapshots, replay, parallel execution, Gymnasium training, native policy playback and desktop training | [M20 acceptance and native RTX 5080 results](https://github.com/particlesector/retro-silicon-emulator/blob/c592dc7857da82cfceceda64d1fc0e27ded713e0/docs/AI_MVP_ACCEPTANCE.md) |

The M20 revision preserves all original chip notes, milestone checklists, test results and workstation reproduction commands. Browse the [historical documentation](https://github.com/particlesector/retro-silicon-emulator/tree/c592dc7857da82cfceceda64d1fc0e27ded713e0/docs) or read a file locally with `git show c592dc7:docs/AI_MVP_ACCEPTANCE.md`.

## How milestones close

- Specify fixtures, expected results and observation boundaries before changing behavior. Keep frozen NES ROM bytes and reference captures versioned. Investigate divergences before updating expectations.
- Configure, build and run affected CTest suites in Debug and Release. Test headless operation independently of SDL3/Vulkan. Exercise packaged Windows/Linux playback when frontend behavior changes.
- Record evidence with the build, platform, procedure and limits. Distinguish automated results from owner-reported gameplay and hardware observations.

Apple IIe chip reuse, broader NES mapper support, a visual workbench and FPGA comparison remain longer-term directions. [Project vision](VISION.md)
