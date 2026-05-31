# terk — Krypton port

Pure-Krypton Windows command runner (mini-terminal).

Compared to the C++ scaffold in the parent directory:
- No Qt. Native Win32 window via Krypton's `stdlib/gui.k`.
- No clang/MSVC. Native PE/COFF build via `kcc -o terk.exe main.k`.
- No C runtime — direct kernel32 IAT calls.

## v0.5 (this build) — what works

- Win32 window with dark RichEdit (monospace, Consolas 11pt).
- Single-line input + Send button + Quit button.
- Each Send spawns `cmd /c "<line> > C:/tmp/terk_out.txt 2>&1"`, waits for
  it to finish, and appends the captured output to the RichEdit. Plain
  CreateProcessA, no pipes — cmd inherits the parent's console.
- Echoes your command into the RichEdit (`> dir`) so the transcript
  reads naturally.

Tested commands that work end-to-end:
- `echo HELLO_WORLD`
- `dir /b C:/Users/brian/Documents`
- `whoami`
- `ver`
- `ipconfig`
- `where cmd`

## v0.5 — what doesn't work (yet)

- **Not a true terminal.** Each Send is a one-shot `cmd /c` invocation;
  there's no persistent shell state. `set FOO=bar` then `echo %FOO%`
  in two separate Sends won't see each other.
- **No ConPTY.** Originally targeted ConPTY (CreatePseudoConsole + cmd.exe
  attached via PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE). The ConPTY setup
  consistently kills the child with STATUS_DLL_INIT_FAILED (0xC0000142).
  Exact cause unidentified — the call sequence matches Microsoft's
  documented pattern byte-for-byte and the intermediate calls all
  return success. May be a Krypton x64.k marshalling subtlety; needs a
  C++ reference comparison to verify. Tracked separately.
- **Raw keystroke input.** No per-keystroke routing to a running shell —
  the Send button is the only way to submit. A real terminal would
  intercept every WM_CHAR via the gui.k WindowProc trampoline.
- **ANSI / cursor handling.** Output is captured plain-text. cmd.exe with
  pipe redirection doesn't emit ANSI anyway.
- **Long-running commands** block the UI for up to 30 seconds
  (WaitForSingleObject timeout). No async / cancel.
- **Resize** doesn't propagate anywhere meaningful.

## v0.5 — Krypton-level surfaces added this session

To make the ConPTY path possible (even though we didn't ship it), the
following landed in Krypton 2.0+:

`compiler/windows_x86/x64.k` — 10 new kernel32 IAT entries with Win32
marshalling table coverage:

  CreatePipe, PeekNamedPipe, CreateProcessA,
  InitializeProcThreadAttributeList, UpdateProcThreadAttribute,
  DeleteProcThreadAttributeList, CreatePseudoConsole,
  ResizePseudoConsole, ClosePseudoConsole, GetExitCodeProcess

`headers/windows.krh` — 10 new function declarations plus
`struct STARTUPINFOA` (104 bytes, x64 layout) and
`struct PROCESS_INFORMATION` (24 bytes) so user code can `import
"head:windows"` and use the full pipe/process/ConPTY surface.

`stdlib/gui.k` — `_guiColorRefFromHex` rewritten in arithmetic to dodge
the bitShr/bitAnd string-operand codegen bug; `guiRichSetBg` now works
again. Whole-Krypton benefit, not terk-specific.

## v0.5 — Krypton bugs surfaced

Discoveries during bring-up, all saved to memory for future fix:

1. `"\\t"` in Krypton string literals produces a TAB byte, not literal
   backslash-t. Affects any Windows path built from string literals.
   Workaround: forward slashes (cmd / Win32 accept them).
2. NULL on raw-pointer Win32 args needs `toHandle("0")` — the Krypton
   string literal `"0"` is a real non-NULL pointer to a "0" string.
3. CreateProcessA with EXTENDED_STARTUPINFO_PRESENT + ConPTY attribute
   list reliably kills the child with 0xC0000142. Not reproduced with
   the Microsoft C++ sample; needs deeper investigation.

## Build

Requires Krypton 2.0+ installed at `C:\krypton` (kcc.exe on PATH) with
the post-2026-05-12 x64.k rebuild (adds the kernel32 surfaces listed
above).

```
build.bat
```

Produces `terk.exe` (~200 KB).

## File layout

```
krypton/
  run.k        — entry point + window + command runner
  build.bat    — invoke kcc native pipeline
  README.md    — this file
```

Krypton convention: `run.k` is the canonical entry-point filename
(matches `just run { ... }`). Other modules import from it; the build
script always passes `run.k` to kcc.

## Architecture (v0.5)

```
┌──────────────────────────────────────────────┐
│ Win32 window (PureKWnd class from gui.k)    │
│                                              │
│  ┌────────────────────────────────────────┐  │
│  │ RichEdit (output, dark, monospace, RO) │  │
│  │                                        │  │
│  │ > dir                                  │  │
│  │ Volume in drive C is ...               │  │
│  │ ...                                    │  │
│  └────────────────────────────────────────┘  │
│                                              │
│  ┌──────────────────────────┐ [Send] [Quit] │
│  │ TextInput (single line)  │               │
│  └──────────────────────────┘               │
└──────────────────────────────────────────────┘
        │
        │ guiGetText → "dir"
        ▼
   cmd /c "dir > C:/tmp/terk_out.txt 2>&1"
        │
        │ WaitForSingleObject
        ▼
   readFile("C:/tmp/terk_out.txt")
        │
        ▼
   guiRichAppend(rich, content, fg, bg)
```

## Next-session work

In priority order, from least-effort to most:

1. **Persistent shell state.** Spawn a single cmd.exe at startup (the
   working pre-ConPTY pattern from trace19) and pipe each line in.
   Won't be ANSI-aware but gives `set X=Y; echo %X%` across commands.
2. **Diagnose the ConPTY 0xC0000142.** Write a C++ reference, compare
   the actual machine code emitted at the CreateProcessA call site.
   If C++ works, the issue is in Krypton x64.k's marshalling — fix
   there. If C++ also fails, file with MS or work around.
3. **Raw keystroke handler in `_krkWndProc`** (gui.k) so the input box
   sends on Enter without needing the Send button click. Real terminal
   feel requires intercepting each key and writing immediately to PTY.
4. **Full ANSI parser** — port `src/parser/AnsiParser.cpp` (179 lines)
   to Krypton; emit RichEdit CHARFORMAT2 sequences for SGR.
5. **Cell grid model** — port `src/renderer/Grid.cpp` (160 lines) to
   pure Krypton; replace RichEdit with a custom WM_PAINT renderer.

## Why no C, even for the PTY?

The 10 kernel32 functions added this session are exposed by the OS, not
the C runtime. Krypton 2.0 calls them directly via the IAT, exactly the
same way `gui.k` calls user32. No libc, no MSVC runtime — just the OS.

The previous C++ scaffold pulled in Qt for windowing (~30 MB of DLLs).
This Krypton port has a 200 KB EXE depending only on `krypton_rt.dll`
and Windows.
