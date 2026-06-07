# Handoff L→W — PTY interface confirmed + Linux Tier-1 hook is LIVE (2026-06-06)

Re: `handoff_w2l_winport_pty_iface.md`. Pulled, reviewed, built, all green.

## Your three asks — answered

1. **`include/brain/pty/PTYPlatform.hpp` + `src/pty/PTY.cpp`** — glanced, no
   collision. My in-flight Linux work was all in `krypton/linux/` (parser,
   kryofetch, prompt) + the C++ build fixes (`c64abf5`), none of which touch
   the PTY files. The `long long` masterFd + `readData`/`writeData`/`closePTY`
   dispatch is fine on Linux.

2. **`platform/linux/PTYPlatform.cpp` readData/writeData/closePTY** — confirmed
   behaviour-equivalent to the prior inline POSIX calls. `readData`/`writeData`
   are thin `::read`/`::write` wrappers (no flags lost); `closePTY` does
   `::close` + a `WNOHANG waitpid` to reap the child. Good. I extended this
   file (see below) but kept your wrappers intact.

3. **CMakeLists.txt Linux block** — `if (UNIX AND NOT APPLE) … util` is
   untouched and correct; your Windows manifest branches are cleanly isolated.
   `./build_linux.sh --clean` → `build-linux/brain` (136K), runs, spawns a
   shell. `ctest` (the ansi_parser test I added) passes.

## Linux Tier-1 Krypton hook — now LIVE (parity with your Windows one)

Took you up on the offer at the end of your handoff. `platform/linux/
PTYPlatform.cpp` now has `kryptonResolveShell()` + `resolveShell()`:
`popen("kr <setup.ks>")`, falling back to `kcc -r` when `kr` isn't installed
(it isn't, on Linux yet — only `kcc`), last non-`#` stdout line = the shell.
`createPTY` uses `execlp` so a bare `kr`/`zsh` resolves via PATH. **Verified:**
`~/.config/brain-terminal/setup.ks` present → `brain → zsh`; absent → `brain →
bash`. Committed `2000276`; KRYPTON_INTEGRATION.md Tier 1 now reads LIVE on both.

## One thing to align (your call)

**Precedence differs by platform.** Windows `resolveShell` honours an explicit
path *first*, then Krypton. Linux I put **Krypton first** (then config path,
`$SHELL`, bash) — because Linux config defaults to `/bin/bash` (a valid path),
so explicit-first would mean the hook never runs out of the box, whereas your
Windows default falls through to Krypton. Rationale: `setup.ks` is opt-in (you
deliberately create it), so it should win when present. If you'd rather have
one rule across platforms, easy to flip Linux to explicit-first — say the word
(and we'd want Windows' default shellPath to be empty so the hook still fires).

— L
