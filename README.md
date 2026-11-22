# SNES Ultimate

SNES Ultimate is a C++20 Super Nintendo Entertainment System emulator for
Windows. It provides a headless runner for regression testing and an SDL3 and
Dear ImGui frontend. The frontend prefers Vulkan, then falls back to Direct3D
12 and OpenGL when the Vulkan runtime or a compatible Vulkan device is absent.

## Features

- 65C816 CPU, S-SMP/SPC700, S-DSP, PPU, DMA/HDMA, scheduler, controllers, and
  cartridge/bus emulation.
- LoROM, HiROM, ExHiROM, SRAM, save states, cheat support, archive loading,
  and common cartridge hardware detection.
- Enhancement-chip implementations and regression coverage for Super FX,
  SA-1, DSP-1 through DSP-4, CX4, ST010/ST011/ST018, S-DD1, SPC7110, S-RTC,
  and ARM6-based cartridges.
- A Windows frontend with Vulkan, Direct3D 12, and OpenGL presentation backends,
  SDL3 input, settings, and video capture.
- A headless executable and an extensive CTest suite for deterministic core,
  timing, and hardware-regression checks.

## Requirements

- CMake 3.16 or newer
- A C++20 compiler (Visual Studio 2022 is supported)
- SDL3 development files for the graphical frontend
- Vulkan SDK 1.4.357.0, or `VULKAN_SDK` / `SNESULTIMATE_VULKAN_SDK_ROOT` set
  to a compatible SDK

Download the SDL3 development package (SDL3-devel-3.4.20-VC) and extract it into the repository root so it is discovered automatically, or pass -DSNESULTIMATE_SDL3_ROOT. The
frontend is currently Windows-only; the core and headless runner can be built
without it.

## Build

### Headless runner

```powershell
cmake -S . -B build-headless -DSNESULTIMATE_BUILD_FRONTEND=OFF
cmake --build build-headless --config Release
```

### Windows frontend

```powershell
cmake -S . -B build
cmake --build build --config Release
```

If the Vulkan SDK is not found automatically, configure with
`-DSNESULTIMATE_VULKAN_SDK_ROOT="C:/path/to/VulkanSDK"`. Set
`-DSNESULTIMATE_SDL3_ROOT="C:/path/to/SDL3"` when using a different SDL3
development package.

## Testing

Run the configured regression suite with:

```powershell
ctest --test-dir build-headless -C Release --output-on-failure
```

Some optional suites need externally supplied test corpora or legally obtained
ROMs. Their configuration options and usage notes are documented in
[`tests/README.md`](tests/README.md).

## Project layout

- `src/core` — emulator components and cartridge hardware
- `src/frontend` — SDL3 desktop frontend and graphics backends
- `src/headless` — command-line runner
- `include` — public internal headers
- `tests` — unit, timing, and regression tests
- `shaders` — Vulkan presentation shaders
- `Firmware` — required coprocessor firmware images

## ROMs and firmware

Use only ROM images and firmware that you are legally entitled to use. ROMs
and locally generated saves, recordings, and build outputs are intentionally
not part of the source tree's documentation or test contract.

## License

No license file is currently included. Do not redistribute the project or its
bundled firmware until licensing terms have been added.
