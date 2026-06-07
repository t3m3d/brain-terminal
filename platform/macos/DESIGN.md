# brain — native macOS frontend

Goal: a native, GPU-rendered macOS terminal in the spirit of Ghostty, not a Qt
app. The existing C++ core is reused untouched; only the frontend is native.

## Architecture

```
        ┌──────────────── reused C++ core (Qt-free) ────────────────┐
PTY ──► PTY/PTYPlatform ──► Terminal ──► AnsiParser ──► Grid (Cells)
        (forkpty, macOS)        │  onPTYOutput()            │
        └────────────────────── │ ──────────────────────── │ ───────┘
                                 ▼ renderCallback           ▼ grid()
        ┌──────────── native Cocoa frontend (platform/macos) ────────┐
        │  main_macos.mm    NSApplication + NSWindow                 │
        │  TermView.mm      NSView: input, CoreText draw, Metal      │
        │  MetalRenderer.mm glyph atlas + batched GPU draw           │
        │  Config.mm        ~/.config/brain/config                   │
        └────────────────────────────────────────────────────────────┘
```

Reused unchanged: `core/Terminal`, `parser/AnsiParser`, `renderer/Grid`+`Cell`,
`pty/PTY`, `scrollback/ScrollbackBuffer`. The macOS layer adds
`platform/macos/PTYPlatform.cpp` (forkpty via `<util.h>`, `TERM=xterm-256color`,
login shell) plus the Cocoa/Metal files above. The core namespace stays `brain::`.

## Build / run

```bash
./platform/macos/build.sh     # clang, no Qt, no CMake -> ./brain.app
open brain.app
```

`open brain.app --args --metal` forces the GPU path (also `BRAIN_RENDERER=metal`
or `renderer = metal` in the config). The CPU CoreText path is the fallback.

## Rendering

Two paths share the same `Grid`. CPU draws per-cell glyphs with CoreText. Metal
rasterizes glyphs once into an R8 atlas and draws the screen as one batched set
of quads (background, glyphs, selection, caret).

Two things that bite in the Metal path:
- The vertex struct field order must match the shader's `VIn` so `float4 color`
  lands 16-byte aligned. Mismatched padding reads garbage and nothing draws.
- `CAMetalLayer` must be layer-hosting (assign `self.layer` before `wantsLayer`),
  and the layer and renderer must share one `MTLDevice`.

Frame pacing: a `CADisplayLink` coalesces bursts of output into one render per
refresh. Grid geometry is cached in a GPU buffer and only rebuilt when content,
scroll, size, or the default colors change; the caret/selection overlay is the
only thing rebuilt on an idle blink.

## State

Working: native Cocoa shell, full SGR (bold/italic/underline/inverse,
16/256/truecolor), UTF-8, scrollback + wheel, mouse selection, copy/paste,
bracketed paste, Metal renderer, config file (font, colors, opacity, renderer),
live font panel (Cmd-T) with Nerd-Font fallback, content-preserving resize.

Parsed but not surfaced: OSC 133 command blocks (marks tracked; gutter UI pulled).

Next: wide-char width, tabs/splits, theme presets, config live-reload, true
scrollback reflow.
