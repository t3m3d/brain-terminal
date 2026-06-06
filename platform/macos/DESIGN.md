# kterm — native macOS frontend

Goal: turn kterm into a nice, native macOS terminal in the spirit of Ghostty
(native, fast, GPU-rendered) — **not** a Qt app. We keep the existing C++ core
and replace only the frontend.

## Architecture

```
        ┌──────────────── reused C++ core (Qt-free) ────────────────┐
PTY ──► PTY/PTYPlatform ──► Terminal ──► AnsiParser ──► Grid (Cells)
        (forkpty, macOS)        │  onPTYOutput()            │
        └────────────────────── │ ──────────────────────── │ ───────┘
                                 ▼ renderCallback           ▼ grid()
        ┌──────────── native Cocoa frontend (platform/macos) ────────┐
        │  main_macos.mm   NSApplication + NSWindow                  │
        │  TermView.mm     NSView: draws Grid, keyboard → PTY        │
        └────────────────────────────────────────────────────────────┘
```

Reused, unchanged: `core/Terminal`, `parser/AnsiParser`, `renderer/Grid`+`Cell`,
`pty/PTY`, `scrollback/ScrollbackBuffer`. Added `platform/macos/PTYPlatform.cpp`
(forkpty via `<util.h>`, sets `TERM=xterm-256color`, login shell) and a cursor
accessor on `Grid`.

## Build / run

```bash
./platform/macos/build.sh     # -> ./kterm-native  (clang, no Qt, no CMake)
./kterm-native
```

## Milestone 1 — native shell (DONE)

Native Cocoa window running a real shell, drawn with CoreText/AppKit:
- macOS PTY (forkpty) spawns `$SHELL` as a login shell.
- PTY output → `Terminal::onPTYOutput` → ANSI parse → `Grid`; repaint marshalled
  to the main thread.
- `TermView` draws the grid (per-cell glyph + bg) with the cell's fg/bg color,
  flipped to top-left origin; translucent block caret.
- Keyboard → PTY: printable chars, Return, Backspace, arrows, Home/End.
- Live resize recomputes cols/rows and notifies the PTY (`TIOCSWINSZ`).

## Known limits (M1) → backlog

- **Rendering is CPU CoreText.** M2 = a **Metal glyph-atlas renderer** (GPU) for
  Ghostty-class speed — the headline next step.
- **ASCII-only cells.** `Cell.ch` is a `char`; widen to a `uint32_t` codepoint
  (+ a width field) for UTF-8 / wide / emoji.
- No scrollback *rendering* (buffer exists; need a scroll viewport + wheel).
- No text attributes (bold/italic/underline/inverse) or cursor styles/blink.
- No selection/copy-paste, tabs, or splits.
- Theme/config not wired into the native frontend yet (hardcoded Menlo 13, dark).

## Roadmap

- **M2** Metal renderer: glyph atlas (CoreText rasterize → texture), instanced
  cell quads, damage tracking. Swap `TermView`'s `drawRect` for a `CAMetalLayer`.
- **M3** Unicode (wide `Cell`), scrollback viewport + smooth scroll, selection +
  clipboard, text attributes, cursor styles.
- **M4** Ghostty-grade polish: ligatures, theme/config files, tabs + splits,
  font fallback, ligature-aware shaping, padding/opacity/blur, fast startup.
