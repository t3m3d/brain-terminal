# overnight push — 2026-06-07 (w)

Brian's directive: "keep going as far as you can tonight at making this a
better terminal that ghostty will be at first."

## Commits landed

1. `6a0a0b2` input/render/mouse — caps fix + startup race + scrollback +
   selection + copy/paste
2. `19acf24` core — title (OSC 0/2), altscreen, DECSC/RC, bell, vim CSI
3. `3090f35` ui — tabs (Ctrl+Shift+T/W, Ctrl+Tab/Shift+Tab, Ctrl+PgUp/Dn)
4. `<polish>` polish — opacity, cursor style, scrollback cap, themes,
   Ctrl+click URL
5. `2667478` links — OSC 8 hyperlinks with id table + underline + Ctrl+click
6. `a51c5ac` find — Ctrl+F bar searches scrollback + live grid
7. `3ba36f4` scroll-region — real DECSTBM (CSI r) honoured by lineFeed

## Original bug list (from your screenshot)

- **Caps displayed but lowercase ran** → `char(Qt::Key_L)` produced
  uppercase; replaced with `e->text()`. Special keys still mapped by
  keycode, Ctrl+letter → C0 byte, Alt+char → ESC <char>.
- **kryofetch first 2.5 lines eaten** → the 350 ms fixed timer beat
  cmd.exe's first prompt paint and the prompt's clear-screen wiped the
  head of kryofetch's banner. Now: defer the startup_command write
  until the first PTY output, then a 120 ms cushion. Matches every
  manual-run case you tested.

## Ghostty-parity features

| feature | status | trigger / notes |
|---------|--------|-----------------|
| mouse selection | yes | click+drag; double-click = word select |
| Ctrl+Shift+C copy | yes | trims trailing blanks per row |
| Ctrl+Shift+V paste | yes | bracketed-paste-aware (ESC[200~/201~) |
| mouse wheel scrollback | yes | 3 lines/notch, clamps to history |
| keystroke → jump to live tail | yes | any key resets viewport offset |
| OSC 0/2 window title | yes | flows to QMainWindow title + tab text |
| altscreen (1049/47/1047) | yes | vim/less/htop preserve main buffer |
| DECSC/RC (ESC 7/8 + CSI s/u) | yes | full save+restore |
| DECSTBM scroll region (CSI r) | yes | lineFeed honours top..bottom |
| CSI L/M @ P X (vim repaint) | yes | insert/delete lines + chars |
| 1J / 1K (erase-to-start) | yes | were falling through to wrong path |
| BEL → visual flash | yes | QApplication::alert; bg-tab marks tab |
| tabs | yes | tab bar auto-hides at count==1 |
| opacity | yes | `opacity = N` in brain.conf, 1..100 |
| cursor style | yes | block / underline / bar |
| scrollback cap | yes | `scrollback = N` (0 disables) |
| themes shipped | yes | default, dracula, nord, solarized-dark, light |
| URL Ctrl+click | yes | regex; cleans trailing punctuation |
| OSC 8 hyperlinks | yes | underline + Ctrl+click resolves cell.link |
| Ctrl+F find | yes | wheel-scroll into view, Enter / Shift+Enter |

## Not in this push

- **Splits / panes.** Big work (splitter tree, focus dance). Ghostty has
  these; we don't yet. Day 2.
- **Sixel / iTerm / Kitty image protocol.** Most users won't notice but
  it'd be useful for `viu`, ranger previews, etc.
- **Ligatures.** Qt's text engine supports them out of the box for fonts
  that ship features, but we use `drawText(QChar)` cell-by-cell so no
  ligature substitution happens. Switching to per-line text layout
  would fix this but is a bigger renderer rewrite.
- **GPU renderer.** QPainter is fine at 27×72. At 4K+ on a big monitor
  with smooth scroll, you'll feel it.

## Things to test when you wake up

Quick smoke list, in increasing complexity. Run from `build-windows/`:

1. Launch `brain.exe`. Should pop titled "brain", land at `%USERPROFILE%`,
   run kryofetch with the FULL `CPU: Intel Core i7-14700KF` line.
2. Type some text. Lower-case should display lower-case. Shift / CapsLock
   should still produce the expected case.
3. Drag-select a chunk. Ctrl+Shift+C. Paste somewhere else — should be
   exact, with trailing-blank trim.
4. Ctrl+Shift+V somewhere. If the shell advertised bracketed paste,
   you'll see it land atomically.
5. Mouse wheel up. Scrollback. Press any key — back to tail.
6. Ctrl+Shift+T. New tab. Ctrl+Tab cycles. Ctrl+Shift+W closes.
7. `vim somefile.txt`. Should open in altscreen; on `:q` your main
   buffer comes back intact.
8. Run something that emits OSC 8 (`ls --hyperlink` on linux through
   ssh; or `gh pr list`). Underlined entries, Ctrl+click opens.
9. Ctrl+F, type a token from kryofetch output. Match highlighted, view
   scrolls to it.

## Build cheatsheet (for future me)

```
cd build-windows
ninja                  # incremental
windeployqt --release brain.exe   # restage Qt runtime DLLs
cp ../resources resources -r      # if themes changed
```

If brain.exe is locked (`Permission denied` link error) — it's still
running. `taskkill /F /IM brain.exe`.

## Files staged in build-windows/

- `brain.exe`
- Qt6Core/Gui/Widgets/Network/Svg.dll
- libgcc_s_seh-1.dll, libstdc++-6.dll, libwinpthread-1.dll
- Qt plugins under `imageformats/`, `iconengines/`, etc. (windeployqt)
- `resources/themes/*.json`, `resources/config/brain.conf`
