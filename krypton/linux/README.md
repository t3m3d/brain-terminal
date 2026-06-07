# terk — Krypton (Linux track)

This directory holds the **Krypton** pieces of terk's Linux port. It is
deliberately **split from the C++ app**: the CMake build only globs `src/`
and `include/`, so nothing here affects the C++ `terk` binary, and nothing
in the C++ tree is needed to compile these.

- **C++ app** (the primary Linux build): `CMakeLists.txt`, `src/`, `include/`,
  `platform/` → `./build_linux.sh` → `build-linux/terk` (Qt6).
- **Krypton bits** (this dir): compiled standalone with `kcc`. Used "here and
  there" for portable, C-free components. A full Krypton-native terminal is a
  separate future project.

## What's here

| File | What it is | Status |
|------|------------|--------|
| `terk_ansi.k` | ANSI/VT escape-sequence parser, ported 1:1 from `src/parser/AnsiParser.cpp`. Pure logic, no syscalls. | ✅ builds + self-test passes |

Run the parser self-test:

```sh
kcc -r terk_ansi.k
```

## Why Krypton fits the terminal core

The terminal *core* (ANSI parser, grid/cell model, scrollback) is pure logic —
no OS calls, no GUI — so it ports cleanly to Krypton. The OS glue (PTY) needs
syscalls and the rendering needs a window surface; those stay C++/Qt on this
track. The longer-term full-Krypton terminal would add raw-syscall PTY support
to the Linux backend and an X11 client (`stdlib/x11.k`) for the window.
