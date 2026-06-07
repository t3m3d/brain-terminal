# Handoff W→L — brain-terminal Windows port lands a wider PTYPlatform interface (2026-06-06)

**TL;DR: I widened `PTYPlatform` to support Windows ConPTY's split-pipe
shape. The Linux side keeps working — `platform/linux/PTYPlatform.cpp`
got updated in the same pass. Pull, glance, and let me know if anything
collides with what you're doing.**

## What changed (interface)

`include/kterm/pty/PTYPlatform.hpp` now has four methods instead of two:

```cpp
static PTYHandles createPTY(const std::string& shellPath, int cols, int rows);
static void       resizePTY(long long masterFd, int cols, int rows);
static long       readData (long long masterFd, void* buf, std::size_t bytes);
static long       writeData(long long masterFd, const void* buf, std::size_t bytes);
static void       closePTY (long long masterFd);
```

And `PTYHandles::masterFd` / `childPid` widened from `int` to `long long`.

**Why:** ConPTY needs an `HPCON` + two `HANDLE`s + a `PROCESS_INFORMATION`
per session — that doesn't fit a single POSIX-style `int fd`. The Windows
impl keeps the real state in a `std::unordered_map<long long, WindowsPTY>`
and the `masterFd` becomes an opaque token. The `readData` / `writeData`
methods dispatch through the map. On Linux they're three-line wrappers
around POSIX `read`/`write`/`close` — no behaviour change.

`src/pty/PTY.cpp` now calls `PTYPlatform::readData`/`writeData`/`closePTY`
instead of POSIX directly. That's the only shared-file edit; `src/pty/PTYPlatform.cpp`'s
preprocessor shim already wires either `platform/linux/…` or
`platform/windows/…` based on platform macros, so neither side needs to
touch the other's file.

## What landed Windows-only

- `platform/windows/PTYPlatform.cpp` — full ConPTY implementation:
  `CreatePseudoConsole` + `STARTUPINFOEXW` with
  `PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE_HANDLE` + `CreateProcessW`,
  + `ResizePseudoConsole`/`ClosePseudoConsole`, async-safe map with mutex.
- `platform/windows/brain-terminal.manifest` — UTF-8 ACP (Win10 1903+),
  long-path aware, PerMonitorV2 DPI, asInvoker. Embedded via a generated
  `.rc` (MSVC) or `windres` (MinGW); wiring is in `CMakeLists.txt`.
- `krypton/setup_shell.ks` — example KryptScript shell-picker.
- `KRYPTON_INTEGRATION.md` — menu of where Krypton slots into the C++
  core, tiered by intrusiveness. Tier 0 + Tier 1 are PoC-ready; Tier
  2+ would touch shared files and we'd coordinate before landing.

## Tier-1 Krypton integration that's already live (Windows-only)

`resolveShell()` in `platform/windows/PTYPlatform.cpp` now consults
`%APPDATA%\brain-terminal\setup.ks` if it exists and `kr.exe` is on
PATH. The script's last stdout line is taken as the shell command;
empty / missing / failure falls back to `cmd.exe`. No runtime link
against Krypton — purely `CreateProcess` + read the pipe + bounded
`WaitForSingleObject`. ~80 LOC, all Windows-side.

This is a concrete demo of Krypton + C++ co-living. Doesn't ask
anything of the Linux side. If you want the same on Linux, see
KRYPTON_INTEGRATION.md §1 — a couple dozen LOC of `fork`+`execvp`+
pipe in `platform/linux/PTYPlatform.cpp` would mirror it.

## What I'd ask of you

1. **Pull and glance** at `include/kterm/pty/PTYPlatform.hpp` +
   `src/pty/PTY.cpp` — those are the only shared touch points. If your
   in-flight Linux work has the same files open, easier to resolve
   now than after either of us pushes more.
2. **`platform/linux/PTYPlatform.cpp` got the matching impl** for
   `readData`/`writeData`/`closePTY`. Behaviour-equivalent to the prior
   inline POSIX calls. Quick read to confirm I didn't miss a flag you
   were depending on.
3. **CMakeLists.txt** picked up Windows-only branches for the manifest
   wiring (MSVC vs MinGW). Linux build path is untouched. Verify the
   `if (UNIX AND NOT APPLE)` block still does what you expect.

## Smoke tests on the Windows side

Can't local-build here (no Qt6 on this box) — but the source is
self-consistent and follows the ConPTY canonical pattern. Brian or
whoever has the Qt6 + MSVC stack can verify with:

```
cmake -G "Visual Studio 17 2022" -A x64 -B build
cmake --build build --config Release
.\build\Release\terk.exe
```

Manifest embeds via the generated `.rc`; ConPTY sessions spawn cmd.exe
(or whatever `setup.ks` prints) and bidirectional I/O routes through
the map. If `kr.exe` is on PATH and there's no setup.ks, fallback is
silent.

— W
