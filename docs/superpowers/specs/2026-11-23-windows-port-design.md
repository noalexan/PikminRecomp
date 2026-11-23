# Windows Port — Design

## Why

Play the recomp natively on Windows (and later ship it through the launcher). Linux stays the
primary platform.

## Approach (my call, user said "go ahead")

**Cross-compile from Linux with Fedora's MinGW-w64 GCC** (`mingw64-gcc-c++`, `mingw64-SDL3`,
`mingw64-vulkan-*`, `mingw64-libpng/zlib/zstd/freetype`). It is the only path verifiable from this
machine, and GCC keeps everything the generated C relies on (`__attribute__((weak))`, `-fwrapv`,
`-ffp-contract=off`, `.incbin`). clang-cl/MSVC (CLAUDE.md's first idea) needs the Windows SDK and a
Windows machine; it stays possible later.

- **Graphics:** Dawn's **Vulkan** backend first (MinGW Vulkan loader); D3D12 only if Dawn builds it
  under MinGW cleanly.
- **Biggest risk, done first:** Dawn + aurora building with MinGW. If that fails, the fallback is
  building on Windows with clang-cl (needs a Windows machine).
- **Verification here:** the `.exe` links; `--headless --fast` boot under Wine (if installed) to the
  same log lines as Linux. The GPU path and real play need a Windows machine (human check).

## Runtime changes (all behind `rt/platform*.{hpp,cpp}`; Linux behaviour unchanged)

| Area | Linux | Windows |
|---|---|---|
| Fibers (`rt/sched.cpp`) | ucontext | Win32 fibers (`ConvertThreadToFiber`, `CreateFiberEx`, `SwitchToFiber`) |
| Disc reads (`rt/disc.cpp`) | `pread` | `_wfopen` + `_fseeki64`/`fread` (UTF-8 → UTF-16 paths) |
| Clock | `CLOCK_MONOTONIC(_COARSE)` | `QueryPerformanceCounter`; `timeBeginPeriod(1)` |
| Executable path | `/proc/self/exe` | `GetModuleFileNameW` |
| Dirs | XDG | `%APPDATA%\pikminrecomp` (config, saves), `%LOCALAPPDATA%\pikminrecomp\logs` |
| Crash handler / watchdog | signals | `SetUnhandledExceptionFilter`, a flag checked at safe points |
| CPU/GPU stats (`rt/sysinfo`) | procfs, NVML | `GetProcessTimes`, NVML (`nvml.dll`) |

## Build

- `cmake -DCMAKE_TOOLCHAIN_FILE=cmake/mingw64.cmake -B build/win` → `pikmin.exe` + DLLs
  (SDL3, libstdc++/winpthreads or static) in an install folder with the data tree.
- Recompiler and tests run on Linux only; generated C is shared.

## Not in scope now

Packaging/installer, Steam on Windows, the launcher (separate design).
