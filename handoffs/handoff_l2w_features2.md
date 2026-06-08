# Handoff L→W — feature wave 2 (themes, live reload, SGR attrs) — 2026-06-07

Picked up the owner's "add the features people enjoy" list. Shipped a batch;
flagging what I touched so we don't re-collide, plus what's still open.

## Landed on main (all build + ctest green)
- **WM-aware tabs** (`18d3910`): `tabs = auto|on|off`. auto disables tabs under
  tiling WMs (Hyprland/sway/i3/river/niri…), keeps them on KDE/GNOME/XFCE.
  Resolves the owner's "no tabs on Hyprland" vs your tab feature — your tab code
  is untouched, just not keybound on tiling WMs.
- **App icon** (`c6a57aa`): embedded via .qrc + `setDesktopFileName("brain")` +
  brain.desktop + hicolor PNGs. Shows in waybar/launcher on Wayland.
- **Themes** (`bbe9973`): theme-by-name (`theme = gruvbox-dark`) + 16-colour
  ANSI `palette` in theme JSON. Shipped Catppuccin/Gruvbox/Tokyo Night + upgraded
  Dracula/Nord/Solarized. **Touched `QtRenderer::loadTheme` (+palette parse, +1
  member array + getter) and `TerminalWidget` setup (apply theme palette).**
- **Live config reload** (`5a1d818`): QFileSystemWatcher on brain.conf + theme →
  re-apply on save. **Added `TerminalWidget::setupConfigWatch/reloadConfig` +
  `Config::sourcePath()`.**
- **SGR strike/dim** (`6ea3d55`): ATTR_STRIKE (9/29) + ATTR_DIM (2/22). **Touched
  `Cell.hpp` (2 new attr bits, now uses bits 16/32), `Terminal.cpp` SGR switch,
  `QtRenderer::drawCell`.**

## Files I edited (heads-up for your next rebase)
`Cell.hpp`, `core/Terminal.cpp` (SGR only), `renderer/QtRenderer.{hpp,cpp}`,
`ui/TerminalWidget.{hpp,cpp}`, `Config.{hpp,cpp}`, resources/themes/*, CMake.
Merged cleanly with your font_weight/use_bold work in Config.cpp.

## Still open from the owner's list (all in the core — let's split)
The owner picked four directions; these remain and overlap your hot files:
1. **Wide-char / emoji / CJK width** — no wcwidth; wide glyphs misalign. Biggest
   item, touches Grid cell advance + cursor + renderer. **Proposing you take this
   if you're already in Grid/Terminal; ping me if not and I'll do it.**
2. **Cursor shape (DECSCUSR `CSI Ps SP q`)** — map to the existing block/bar/
   underline renderer styles. I can take this (parser + Terminal + a callback).
3. **Inline images (Sixel)** — new subsystem. Unclaimed; big.
4. **URL auto-detection + OSC 52 (clipboard) + OSC 7 (cwd)** — OSC 52/7 are in
   `Terminal.cpp` OSC handling (your lane via altscreen/links); URL auto-detect
   is in `TerminalWidget` near your OSC 8 click handling. **You likely want
   these since you own the link path — tell me and I'll stay clear.**

Say which of 1–4 you're taking and I'll take the rest without overlapping.
— L
