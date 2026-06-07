# Building terk on Linux

terk is a Qt6 / C++20 terminal emulator that already compiles
cross-platform via CMake. This doc + the companion `build_linux.sh`
codify the Linux build path so you don't have to remember the cmake
flags. Tested under **Arch on WSL2** for parity with the Krypton
agent-w track; should work on native Arch, Debian, Ubuntu, and any
distro with Qt6 packages.

## Quick start

```bash
# 1. Install prerequisites (see distro-specific notes below)
# 2. Run the build script
./build_linux.sh

# Optional: build then immediately launch
./build_linux.sh --run

# Optional: build, then build + run the ctest suite
./build_linux.sh --test

# Optional: wipe build-linux/ before configuring (after CMakeLists changes)
./build_linux.sh --clean
```

The script puts everything in `./build-linux/` and produces
`./build-linux/terk`. Nothing is installed system-wide.

**Verified working** (2026-06-06): builds, links, runs, and spawns a shell
through the PTY on Arch Linux (zen kernel) with **cmake 4.3.3 / gcc 16.1.1 /
Qt6 6.11.1**. The Qt `AUTOMOC` step is wired in `CMakeLists.txt` (the
`TerminalWidget` `Q_OBJECT` needs it). A Krypton shell-pick hook is live (see
`KRYPTON_INTEGRATION.md`); the Krypton bits live in `krypton/` and never touch
the C++ build (CMake only globs `src/` + `include/`).

## Prerequisites

You need a C++20 compiler, CMake ≥ 3.21, and Qt6 (`Core`, `Gui`,
`Widgets` modules). On Linux/WSL the build also links against
`libutil` for PTY support (the `forkpty` / `openpty` family in
`platform/linux/PTYPlatform.cpp`).

### Arch / Arch on WSL2

```bash
sudo pacman -S --needed base-devel cmake qt6-base
```

`base-devel` covers gcc + make + binutils. `qt6-base` gives you
Core/Gui/Widgets. The PTY helpers in `glibc` cover `libutil`
implicitly on Arch (the standalone `libutil` package was retired).

### Debian / Ubuntu / ParrotOS

```bash
sudo apt install build-essential cmake qt6-base-dev libutil-dev
```

`build-essential` is the gcc/make bundle. `qt6-base-dev` provides
the cmake config files for Qt6's modules. `libutil-dev` provides the
header for `forkpty`.

### WSL2 specific

- **WSL2 only** — the build itself works fine under WSL1, but terk
  spawns child processes through PTYs and uses some syscalls WSL1
  emulates poorly. Run inside `wsl --version` ≥ 2.
- WSL2 picks up X / Wayland automatically on Windows 11 (WSLg). If
  you're on Windows 10, install an X server (VcXsrv, X410) and set
  `DISPLAY` before launching.

## What's where

| Path | Purpose |
|---|---|
| `CMakeLists.txt` | Cross-platform build definition. UNIX-not-APPLE branch links `libutil`. |
| `platform/linux/PTYPlatform.cpp` | Linux PTY backend (forkpty + waitpid). Already implemented. |
| `platform/windows/` | Windows ConPTY backend. Not used on Linux. |
| `src/` | Application code. `file(GLOB_RECURSE)` picks it all up. |
| `include/` | Headers, organised into `collect/`, `render/`, `platform/`, `utils/`, `branding/`. |
| `resources/config/terk.conf` | Default config. Installs to `share/terk/` on `make install`. |
| `resources/themes/` | Colour themes directory. Installs to `share/terk/themes/`. |

## Build modes

The script defaults to `Release`. To configure differently, just
invoke cmake manually:

```bash
mkdir build-debug && cd build-debug
cmake -DCMAKE_BUILD_TYPE=Debug ..
cmake --build . -- -j$(nproc)
```

The script's `--clean` re-creates `build-linux/` fresh — useful when
you've added new `src/` files (CMake's `file(GLOB_RECURSE)` only re-
scans on configure, not on incremental builds).

## Install (system-wide, optional)

```bash
cd build-linux
sudo cmake --install . --prefix /usr/local
```

That puts `terk` in `/usr/local/bin/` and the config/themes under
`/usr/local/share/terk/`.

## Troubleshooting

- **`Could NOT find Qt6` from cmake** — `qt6-base` (Arch) or
  `qt6-base-dev` (Debian) not installed. Re-check the Prerequisites
  section. Alternatively, point cmake at a non-standard Qt
  installation with `-DCMAKE_PREFIX_PATH=/opt/Qt/6.x.x/gcc_64`.
- **`fatal error: pty.h: No such file or directory`** — on Debian,
  install `libutil-dev`. On Arch the header is in glibc and should
  always be present.
- **WSLg / GUI not appearing** — under WSL1 you need an external X
  server. Under WSL2 on Windows 10, install VcXsrv and
  `export DISPLAY=:0`. Under WSL2 on Windows 11 GUI apps should
  "just work" via WSLg.
- **Linker complains about `Ws2_32` / `Iphlpapi`** — you've somehow
  triggered the `if (WIN32)` branch in `CMakeLists.txt`. Make sure
  you're configuring through `cmake` (not Visual Studio's WSL
  remote tool with mixed paths).

## See also

- `BUILD_WINDOWS.md` (if present) — the Windows / MSVC build flow.
- Agent w's Linux handoff (`krypton/AGENT_W_GREENFIELD.md`) — the
  broader Linux track this fits into.
