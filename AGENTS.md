# Agent development instructions

Project: Retro Silicon Emulator (Apache-2.0).

1. Preserve strict separation of core, system models, CLI and SDL3/Vulkan frontend.
2. Treat accuracy and deterministic testing as higher priority than feature breadth.
3. Implement small reviewable changes with unit tests.
4. Do not claim NES emulation, FPGA equivalence or Vulkan rendering until verified.
5. Run CMake configure, build and CTest when a build environment is available.
6. Avoid bundling copyrighted game ROMs or proprietary firmware.
7. Keep public interfaces documented and avoid premature plugin ABI commitments.
