# Retro Silicon Emulator

A C++ emulator project with a deterministic core, a CLI, and an optional SDL3
desktop frontend.

## Requirements

- CMake 3.25 or newer and a C++23 compiler.
- On Windows: Visual Studio 2022 with the **Desktop development with C++**
  workload and a Windows SDK.
- On Linux: GCC or Clang with C++23 support and Ninja. Install your distribution's
  C++ development tools, CMake, and Ninja packages.
- For the optional desktop frontend: SDL3 3.2 or newer development files and the Vulkan SDK.

## Build and test

Run the commands for your platform from the repository root.

### Windows

In PowerShell:

```powershell
cmake -S . -B build/vs -G "Visual Studio 17 2022" -A x64
cmake --build build/vs --config Debug
ctest --test-dir build/vs -C Debug --output-on-failure
```

Show CLI help and version information:

```powershell
.\build\vs\Debug\rse-cli.exe --help
.\build\vs\Debug\rse-cli.exe --version
```

To include the desktop frontend, add `-DRSE_BUILD_DESKTOP=ON` to the configure
command. SDL3 and Vulkan must be discoverable by CMake; set `SDL3_DIR` to the
directory containing `SDL3Config.cmake` if needed, and set `RSE_SDL_LICENSE` to
SDL3's `LICENSE.txt` for runtime packaging. The desktop executable is
`build/vs/Debug/rse-desktop.exe`.

### Linux

With CMake, Ninja, and your C++ compiler on `PATH`, use the included Debug preset:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Run the CLI:

```sh
./build/dev/rse-cli --help
./build/dev/rse-cli --version
```

For the optional desktop frontend, install SDL3 and Vulkan development packages
and configure with `cmake --preset dev -DRSE_BUILD_DESKTOP=ON`. Build with the
same preset commands above, supplying `-DRSE_SDL_LICENSE=/path/to/SDL/LICENSE.txt`,
then run `./build/dev/rse-desktop`.

The Ninja preset also works on Windows from a Visual Studio Developer PowerShell.

## Project guides

- [Development roadmap and completed milestones](docs/ROADMAP.md)
- [Game Boy Advance and LeafGreen roadmap](docs/GBA_ROADMAP.md)
- [Architecture](docs/ARCHITECTURE.md) and [machine sessions](docs/SESSIONS.md)
- [NES support and CLI](docs/NES_SYSTEM.md) and [desktop controls and packages](docs/DESKTOP.md)
- [Inspection and automation](docs/AUTOMATION.md) and [training and policy playback](docs/TRAINING_SDK.md)

Applications default to `nes-ntsc`. Licensed under [Apache-2.0](LICENSE).
