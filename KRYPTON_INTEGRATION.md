# Krypton-lang ↔ brain-terminal integration notes

brain-terminal is a C++/Qt6 codebase. Krypton (`kcc` / `kr` / `.ks` scripts)
slots in cleanly at the **edges** — places where the C++ core hands off to
user-customisable behaviour. This file is the menu of natural integration
points, ordered by how much shared code each one touches.

## Tier 0 — works today, no C++ changes

1. **Krypton scripts run inside the terminal.** brain-terminal spawns whatever
   shell the user configures. Point it at `kr.exe` instead of `cmd.exe` and
   you get an accumulating KryptScript REPL as your terminal's primary shell.
   Or point it at any `.ks` script and the terminal runs that script as its
   "session." Already works because ConPTY is shell-agnostic.

2. **`.ks` scripts the user invokes from inside the terminal.** Anything the
   2.3.0 toolchain supports (kp, len, sbAppend, file I/O, sockets, …)
   runs unmodified. The terminal is just the host.

## Tier 1 — small Windows-only hook (this is what's PoC-ready)

3. **Config / shell-pick override via a Krypton script.** Convention:
   `%APPDATA%\brain-terminal\setup.ks` (or `~/.config/brain-terminal/setup.ks`
   on Linux). When `PTYPlatform::createPTY` resolves a shell, it can read
   the script's last-line output via `kr.exe <script>` and use that path.
   That lets a user write a Krypton script that picks `bash` vs `pwsh` vs
   `cmd` based on whether their git repo has a `.envrc`, the time of day,
   etc. — programmatic config without us baking in an expression language.

   See `krypton/setup_shell.ks` for the template.

   **Code shape (Windows-only, in `platform/windows/PTYPlatform.cpp`):**
   ```cpp
   std::optional<std::string> kryptonResolvedShell() {
       // GetEnvironmentVariableW("APPDATA") → %APPDATA%\brain-terminal\setup.ks
       // CreateProcess("kr.exe <script>"), capture stdout, last line = path.
       // Return std::nullopt if kr.exe isn't on PATH or script absent.
   }
   ```
   Add to `resolveShell()` as the **first** lookup; cmd.exe stays the
   fallback when Krypton tooling isn't installed. ~30 LOC.

## Tier 2 — shared, talk to Linux first

4. **Themes as `.k` modules.** Today themes are `.conf` files. A `theme.k`
   could compute palette entries from a base hue, return a struct the C++
   `Theme` class consumes via JSON over stdout. Touches
   `src/theme/ThemeLoader.cpp` — shared with Linux. **Coordinate before
   landing.**

5. **`kls`-style status providers.** A `~/.config/brain-terminal/status.ks`
   that runs on a timer, prints a JSON line to stdout, brain-terminal
   renders it in a status bar. Touches `src/TerminalWindow.cpp`.

6. **Embedded REPL pane.** A second widget hosting an in-process
   `kr` REPL. Touches the renderer + input plumbing — non-trivial.

## Tier 3 — long-arc

7. **brain-terminal itself partially in Krypton.** Krypton's `stdlib/gui.k`
   already does Win32; an X11 backend is in progress on the Linux side
   (`stdlib/x11.k` Phase A2). When both stabilise, the **window
   chrome + tab strip + status bar** could be Krypton with the renderer
   staying C++. That's the v2.0 of brain-terminal, not the v1.0 port.

## Build coupling

- **Runtime dependency on Krypton:** zero. Tiers 1+ shell out to `kr.exe`
  via `CreateProcess`; nothing links against `krypton_rt.dll`. If Krypton
  isn't installed, brain-terminal falls back to its built-in behaviour.
- **Installer:** the brain-terminal Windows installer could recommend
  Krypton as an optional companion (one checkbox, downloads from
  krypton-lang.org). Strictly optional.

## Why this shape

The C++ core stays small, fast, and self-contained. Krypton handles the
parts that benefit from a real scripting language — branching config,
programmable themes, runtime extensibility — without bolting an interpreter
into the C++ build. The interop boundary is `CreateProcess` + stdout / a
file the C++ side reads. That's testable, replaceable, and works the same
way on Windows and Linux.
