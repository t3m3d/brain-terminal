# brain — Krypton (Linux track)

This directory holds the **Krypton** pieces of brain's Linux port. It is
deliberately **split from the C++ app**: the CMake build only globs `src/`
and `include/`, so nothing here affects the C++ `brain` binary, and nothing
in the C++ tree is needed to compile these.

- **C++ app** (the primary Linux build): `CMakeLists.txt`, `src/`, `include/`,
  `platform/` → `./build_linux.sh` → `build-linux/brain` (Qt6).
- **Krypton bits** (this dir): compiled standalone with `kcc`. Used "here and
  there" for portable, C-free components. A full Krypton-native terminal is a
  separate future project.

## What's here

| File | What it is | Status |
|------|------------|--------|
| `brain_ansi.k` | ANSI/VT escape-sequence parser, ported 1:1 from `src/parser/AnsiParser.cpp`. Pure logic, no syscalls. | ✅ builds + self-test passes |
| `kryofetch.k` | neofetch-style system-info command (Krypton crystal logo + ANSI colour), reads real Linux info via `exec()`/`/proc`. A brain built-in. The repo's static `krypton/kryofetch` was a Windows sample; this is the working Linux one. | ✅ runs |
| `brain_prompt.k` | native shell prompt: user@host, `~`-shortened cwd, git branch + dirty `*`, exit-status-coloured `❯`, and an OSC 133 prompt-start marker (lights up brain's command-block UI). | ✅ runs |

Run them:

```sh
kcc -r brain_ansi.k                 # parser self-test
kcc -r kryofetch.k                 # system info
kcc -o brain_prompt brain_prompt.k   # compile the prompt, then:
#   bash: PS1='$(brain_prompt $?)'  ($? passes the last exit code as arg 0)
```

Notes on Krypton's Linux shell surface (learned building these):
`exec(cmd)` **captures** stdout; `shellRun(cmd)` **streams** it and returns an
exit code. `readFile` on a `/proc` virtual file returns empty (zero stat size) —
read those via `exec("cat /proc/...")` instead. The native list-returning
`splitBy` is unreliable, so these use hand-rolled field scanners.

## Why Krypton fits the terminal core

The terminal *core* (ANSI parser, grid/cell model, scrollback) is pure logic —
no OS calls, no GUI — so it ports cleanly to Krypton. The OS glue (PTY) needs
syscalls and the rendering needs a window surface; those stay C++/Qt on this
track. The longer-term full-Krypton terminal would add raw-syscall PTY support
to the Linux backend and an X11 client (`stdlib/x11.k`) for the window.
