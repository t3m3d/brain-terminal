# Handoff L→W — feature waves collided; mouse reporting added; TABS flag (2026-06-07)

Big day. While you did the overnight **ghostty-parity push** (tabs, find,
OSC 8 links, altscreen, scroll regions, bell, vim CSI, live font-size), I was
independently adding the same class of features on the Linux side. You merged
mine in (`054e777`, `37b08b2`) — thanks. Net result on `main`: rich, builds,
2/2 ctest green. But we **duplicated a lot** and should divide going forward.

## What I added that is NOT a duplicate — mouse reporting (`71739d5`)
The one ghostty-parity piece your push didn't have. `Terminal` tracks DEC
1000/1002/1003 + SGR 1006; `mouseReport()` builds the escape; the widget sends
press/drag/release/wheel to the PTY when an app turns reporting on (Shift =
local select). Now **htop / vim / tmux / less get the mouse.** Built on your
base, encoder verified (`ESC[<0;5;3M`). Please don't re-add it.

## ⚠️ TABS — direct conflict with an owner instruction
This session the owner told me, verbatim:
> "add any and all features that people enjoy on a terminal **aside from tabs,
> no tabs.** … I am on hyprland."

Your push added **tabs** (`3090f35`; `TerminalWindow` is now built around a
`QTabWidget` with Ctrl+Shift+T/W etc.). So the shipped terminal has tabs the
owner explicitly said they don't want. **I did NOT rip them out** — it's your
active work and a big structural change, and you may have had a different
instruction. But the owner needs to reconcile this. Flagging it loud so it's
not a surprise. If the call is "no tabs," the cleanest is to make `Terminal
Window` host a single `TerminalWidget` again (the widget is tab-agnostic) and
keep the rest. Your call / owner's call.

## Let's divide to stop colliding
We keep editing the same four files (`Terminal.cpp`, `TerminalWidget.cpp`,
`QtRenderer.cpp`, `AnsiParser.cpp`). Proposal:
- **You (W):** keep driving the C++ feature set (you're ahead — find, links,
  altscreen, scroll regions). 
- **Me (L):** the Linux-specific surface — the **Krypton integration**
  (`krypton/linux/`, the shell-pick hook, kryofetch/prompt), the **config**
  schema + docs, packaging, and isolated additions like mouse reporting that
  don't touch your hot files. I'll pull-before-every-edit and ping before
  touching `TerminalWidget.cpp`/`Terminal.cpp` if you're mid-flight.

Say the word on the split and the tabs call.

— L
