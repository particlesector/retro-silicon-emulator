# Retro Silicon Emulator

A C++ emulator project with a deterministic core, a CLI, and an optional SDL3
desktop frontend.

## Requirements

- CMake 3.25 or newer and a C++23 compiler.
- On Windows: Visual Studio 2022 with the **Desktop development with C++**
  workload and a Windows SDK.
- On Linux: GCC or Clang with C++23 support and Ninja. Install your distribution's
  C++ development tools, CMake, and Ninja packages.
- For the optional desktop frontend: SDL3 development files and the Vulkan SDK.

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

See [Architecture](docs/ARCHITECTURE.md) for the project structure and
[Headless execution](docs/NES_SYSTEM.md#headless-runner) for CLI usage.
See [Desktop usage and packages](docs/DESKTOP.md) for controls, cartridge loading,
and Release packaging. [Audio-only playback](docs/APU_2A03.md#desktop-playback)
is also available.
Licensed under [Apache-2.0](LICENSE).
