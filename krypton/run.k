// brain/krypton/run.k — Krypton-native Windows mini-terminal.
//
// v0.6: drop the separate input + Send button. RichEdit is now WRITABLE
// and fills the whole window. After each command we append a
// `user@HOST$ ` prompt at the end. User types at the cursor (which lives
// at the end of the buffer); when they press Enter, the resulting `\n`
// in the RichEdit content triggers our timer-poll path to extract the
// command since the last prompt, run it, and append output + new prompt.
//
// Notes:
//   - This still uses one-shot `cmd /c <line>` under the hood (the v0.5
//     mechanism). No persistent shell state across commands.
//   - The whole "terminal feel" is a single-RichEdit illusion: there's
//     no separate input control. If the user clicks INTO the existing
//     output to move the cursor, they can edit history. That's expected.
//     A future revision could subclass the RichEdit to block edits
//     before the prompt position.

import "head:windows"
import "head:uxtheme"
import "k:gui"

func ctxSet(key, value) { guiStateSet("brain." + key, value) emit "" }
func ctxGet(key)        { emit guiStateGet("brain." + key) }

// Forward slashes in the path — Krypton's string-literal escapes treat
// "\\t" as TAB (feedback_krypton_string_escapes.md), and cmd accepts /.
let TMP_OUT = "C:/tmp/brain_out.txt"


// ── Win32: user + host name ────────────────────────────────────

func makePrompt() {
    let userBuf = bufNew("256")
    let userLenBuf = bufNew("4")
    bufSetDwordAt(userLenBuf, "0", "256")
    GetUserNameA(userBuf, userLenBuf)
    let user = bufStr(userBuf)

    let hostBuf = bufNew("256")
    let hostLenBuf = bufNew("4")
    bufSetDwordAt(hostLenBuf, "0", "256")
    GetComputerNameA(hostBuf, hostLenBuf)
    let host = bufStr(hostBuf)

    emit user + "@" + host + "$ "
}


// ── cmd runner ────────────────────────────────────────────────

// Run a single command line and return its output. Goes through PowerShell
// so users get the Linux-style aliases (ls, cd, pwd, cat, rm, mv, cp, etc)
// that PowerShell ships with by default — same behavior as Windows Terminal.
// `-NoProfile` skips ~/.psrc loading for faster startup.
func runCmdLine(line) {
    let heap = GetProcessHeap()
    let dq = fromCharCode("34")
    // PowerShell's -Command takes a script. Wrap user input in single quotes
    // (escaped), or use a simpler invocation with -Command "& { <line> }".
    // Output is redirected via a separate cmd shell since direct PS output
    // redirect needs encoding management.
    let full = "powershell.exe -NoProfile -NonInteractive -Command " + dq + "& { " + line + " }" + dq + " > " + TMP_OUT + " 2>&1"
    // Have to wrap the whole thing in cmd /c so the redirect operates outside PS.
    let wrapped = "cmd /c " + dq + full + dq
    let cmdBuf = HeapAlloc(heap, "0", (len(wrapped) + 1) + "")
    let i = 0
    while i < len(wrapped) {
        bufSetByte(cmdBuf, i + "", charCode(wrapped[i]))
        i = i + 1
    }
    bufSetByte(cmdBuf, i + "", "0")

    let si = bufNew("104")
    bufSetDwordAt(si, "0", "104")
    let pi = bufNew("24")
    let r = CreateProcessA(toHandle("0"), cmdBuf, "0", "0", "0", "0", "0", toHandle("0"), si, pi)
    if r == "0" { emit "[error: CreateProcessA failed]" }
    let hProc = bufGetQwordAt(pi, "0")
    WaitForSingleObject(hProc, "30000")
    CloseHandle(hProc)
    emit readFile(TMP_OUT)
}


// ── RichEdit helpers ─────────────────────────────────────────

func richHwnd()         { emit _guiResolveHwnd(ctxGet("ui.output")) }
func richLength()       { emit toInt(SendMessageA(richHwnd(), "14", "0", "0")) }   // WM_GETTEXTLENGTH

// Pull the chars between offsets `from` and `to` out of the RichEdit.
// Uses WM_GETTEXT into a heap buf; bufStr trims at NUL.
func richSubstring(from, to) {
    let len_ = to - from
    if len_ <= 0 { emit "" }
    let h = richHwnd()
    let full = richLength()
    let buf = bufNew((full + 1) + "")
    SendMessageA(h, "13", (full + 1) + "", ptrToInt(buf))
    let s = bufStr(buf)
    emit substring(s, from, to)
}

// PLACEHOLDER — kept so compile doesn't break references below. The
// proper UTF-16 path needs SendMessageW + MultiByteToWideChar in the IAT
// (deferred until x64.k can be safely extended). Until then, callers go
// through utf8ToAscii + guiRichAppend.
func utf8ToUtf16Buf(s) {
    let n = len(s)
    let buf = bufNew((n * 2 + 4) + "")
    let i = 0
    let out = 0
    while i < n {
        let c = charCode(s[i])
        if c < 0 { c = c + 256 }
        let cp = 63
        let inc = 1
        if c < 128 {
            cp = c
            inc = 1
        } else if c >= 192 && c <= 223 {
            let c2 = 0
            if i + 1 < n {
                c2 = charCode(s[i + 1])
                if c2 < 0 { c2 = c2 + 256 }
            }
            cp = (c - 192) * 64 + (c2 - 128)
            inc = 2
        } else if c >= 224 && c <= 239 {
            let c2 = 0
            let c3 = 0
            if i + 1 < n {
                c2 = charCode(s[i + 1])
                if c2 < 0 { c2 = c2 + 256 }
            }
            if i + 2 < n {
                c3 = charCode(s[i + 2])
                if c3 < 0 { c3 = c3 + 256 }
            }
            cp = (c - 224) * 4096 + (c2 - 128) * 64 + (c3 - 128)
            inc = 3
        } else if c >= 240 && c <= 247 {
            let c2 = 0
            let c3 = 0
            let c4 = 0
            if i + 1 < n {
                c2 = charCode(s[i + 1])
                if c2 < 0 { c2 = c2 + 256 }
            }
            if i + 2 < n {
                c3 = charCode(s[i + 2])
                if c3 < 0 { c3 = c3 + 256 }
            }
            if i + 3 < n {
                c4 = charCode(s[i + 3])
                if c4 < 0 { c4 = c4 + 256 }
            }
            cp = (c - 240) * 262144 + (c2 - 128) * 4096 + (c3 - 128) * 64 + (c4 - 128)
            inc = 4
        }
        if cp <= 65535 {
            bufSetByte(buf, out + "", (cp % 256) + "")
            bufSetByte(buf, (out + 1) + "", (cp / 256) + "")
            out = out + 2
        } else {
            let adj = cp - 65536
            let hi = 55296 + adj / 1024
            let lo = 56320 + adj % 1024
            bufSetByte(buf, out + "", (hi % 256) + "")
            bufSetByte(buf, (out + 1) + "", (hi / 256) + "")
            bufSetByte(buf, (out + 2) + "", (lo % 256) + "")
            bufSetByte(buf, (out + 3) + "", (lo / 256) + "")
            out = out + 4
        }
        i = i + inc
    }
    bufSetByte(buf, out + "", "0")
    bufSetByte(buf, (out + 1) + "", "0")
    emit buf
}

// CP437-byte path: utf8ToAscii maps UTF-8 block/box chars to CP437 byte
// values, then guiRichAppend inserts. Conversion uses the RichEdit's
// font charset, which was set to OEM_CHARSET in setRichFontOem.
// UTF-8 path via process manifest: brain.exe.manifest declares
// activeCodePage=UTF-8, so SendMessageA's ANSI→Unicode conversion uses
// CP_UTF8 (65001) instead of CP1252. Block / box / degree chars render
// as their actual Unicode glyphs without needing SendMessageW.
// (Win10 1903+ feature — pre-1903 falls back to CP1252 mojibake.)
func richAppendUtf8(rich, text, hexColor, bold) {
    guiRichAppend(rich, text, hexColor, bold)
    emit "1"
}

// Create a monospace font with lfCharSet = OEM_CHARSET (255) and bind it
// to the RichEdit via WM_SETFONT. Per MSDN, EM_REPLACESEL converts ANSI
// bytes to Unicode using the codepage of the SELECTION'S FONT — so this
// makes byte 0xDB → U+2588 (█), 0xB1 → U+2592 (▒), etc. Replaces
// guiRichSetMonoFont (which uses DEFAULT_CHARSET = system ANSI = CP1252
// on US Windows, giving us Û and ± instead of block chars).
func setRichFontOem(rich, family, ptSize) {
    let h = _guiResolveHwnd(rich)
    if h == "0" { emit "0" }
    let lf = bufNew("60")
    let neg = 0 - toInt(ptSize)
    bufSetDwordAt(lf, "0", neg + "")     // lfHeight (negative for pixels)
    bufSetDwordAt(lf, "16", "400")       // lfWeight = FW_NORMAL
    bufSetByte(lf, "23", "255")          // lfCharSet = OEM_CHARSET
    bufSetByte(lf, "26", "5")            // lfQuality = CLEARTYPE_QUALITY
    bufSetByte(lf, "27", "1")            // lfPitchAndFamily = FIXED_PITCH
    let i = 0
    while i < len(family) {
        if i < 31 {
            bufSetByte(lf, 28 + i, charCode(family[i]))
        }
        i = i + 1
    }
    let hf = CreateFontIndirectA(lf)
    SendMessageA(h, "48", hf, "1")       // WM_SETFONT, fRedraw=TRUE
    emit "1"
}

// Map an SGR code to a hex color string. Default fg is our terminal-text
// color (0xE0E8F0). 30-37 standard, 90-97 bright, 39 default fg.
// 256-color (38;5;N) and truecolor (38;2;R;G;B) are not yet handled — they
// fall through to default. CFG_DEFAULT is the unset sentinel.
func ansiFgForCode(code) {
    if code == 30 { emit "0x222222" }
    if code == 31 { emit "0xCD3131" }
    if code == 32 { emit "0x0DBC79" }
    if code == 33 { emit "0xE5E510" }
    if code == 34 { emit "0x2472C8" }
    if code == 35 { emit "0xBC3FBC" }
    if code == 36 { emit "0x11A8CD" }
    if code == 37 { emit "0xE5E5E5" }
    if code == 90 { emit "0x666666" }
    if code == 91 { emit "0xF14C4C" }
    if code == 92 { emit "0x23D18B" }
    if code == 93 { emit "0xF5F543" }
    if code == 94 { emit "0x3B8EEA" }
    if code == 95 { emit "0xD670D6" }
    if code == 96 { emit "0x29B8DB" }
    if code == 97 { emit "0xFFFFFF" }
    emit "0xE0E8F0"
}

// Render text containing ANSI/VT100 escape sequences into the RichEdit.
// Splits into runs of same-fg text, calls guiRichAppend per run. SGR
// codes update fg + bold state; CSI non-m sequences (cursor moves, etc)
// are skipped; OSC sequences are skipped; CR is dropped. Unhandled
// codes leave state unchanged.
func renderAnsi(rich, s) {
    let n = len(s)
    let i = 0
    let chunk = sbNew()
    let chunkEmpty = "1"
    let curFg = "0xE0E8F0"
    let curBold = "0"
    while i < n {
        let c = charCode(s[i])
        if c == 13 {
            i = i + 1
        } else if c == 27 {
            if chunkEmpty == "0" {
                richAppendUtf8(rich, sbToString(chunk), curFg, curBold)
                chunk = sbNew()
                chunkEmpty = "1"
            }
            if i + 1 >= n {
                i = n
            } else if s[i + 1] == "[" {
                let j = i + 2
                let params = sbNew()
                let final = ""
                while j < n && final == "" {
                    let cj = charCode(s[j])
                    if cj >= 64 && cj <= 126 {
                        final = s[j]
                    } else {
                        params = sbAppend(params, s[j])
                        j = j + 1
                    }
                }
                if final == "" {
                    i = n
                } else {
                    if final == "m" {
                        let paramsStr = sbToString(params)
                        if len(paramsStr) == 0 { paramsStr = "0" }
                        let pn = len(paramsStr)
                        let p = 0
                        let code = 0
                        let inCode = "0"
                        while p <= pn {
                            let isSep = "0"
                            if p == pn {
                                isSep = "1"
                            } else {
                                if paramsStr[p] == ";" { isSep = "1" }
                            }
                            if isSep == "1" {
                                if inCode == "1" {
                                    // Bold only; ignore color codes entirely
                                    // so the whole transcript renders in
                                    // default fg (grey/white via natural glyph
                                    // intensity). No green labels, no blue
                                    // ASCII art, no red/yellow/cyan accents.
                                    if code == 0 {
                                        curBold = "0"
                                    } else if code == 1 {
                                        curBold = "1"
                                    } else if code == 22 {
                                        curBold = "0"
                                    }
                                }
                                code = 0
                                inCode = "0"
                            } else {
                                let cc = charCode(paramsStr[p])
                                if cc >= 48 && cc <= 57 {
                                    code = code * 10 + (cc - 48)
                                    inCode = "1"
                                }
                            }
                            p = p + 1
                        }
                    }
                    i = j + 1
                }
            } else if s[i + 1] == "]" {
                let j = i + 2
                let done = "0"
                while j < n && done == "0" {
                    let cj = charCode(s[j])
                    if cj == 7 { i = j + 1  done = "1" }
                    else { j = j + 1 }
                }
                if done == "0" { i = n }
            } else {
                i = i + 2
            }
        } else {
            // Detect █ (UTF-8 E2 96 88) and render those specifically in
            // grey, so kryofetch's "used" bar segments appear grey instead
            // of full white. Everything else (▒/░/▓, ASCII, etc.) stays at
            // curFg. Batch consecutive █'s into one run for efficiency.
            let isFullBlock = "0"
            if c == 226 && i + 2 < n {
                let cc2 = charCode(s[i + 1])
                if cc2 < 0 { cc2 = cc2 + 256 }
                if cc2 == 150 {
                    let cc3 = charCode(s[i + 2])
                    if cc3 < 0 { cc3 = cc3 + 256 }
                    if cc3 == 136 { isFullBlock = "1" }
                }
            }
            if isFullBlock == "1" {
                if chunkEmpty == "0" {
                    richAppendUtf8(rich, sbToString(chunk), curFg, curBold)
                    chunk = sbNew()
                    chunkEmpty = "1"
                }
                let runBuf = sbNew()
                let stillBlock = "1"
                while stillBlock == "1" && i + 2 < n {
                    let bc = charCode(s[i])
                    if bc < 0 { bc = bc + 256 }
                    if bc != 226 { stillBlock = "0" }
                    if stillBlock == "1" {
                        let bc2 = charCode(s[i + 1])
                        if bc2 < 0 { bc2 = bc2 + 256 }
                        if bc2 != 150 { stillBlock = "0" }
                    }
                    if stillBlock == "1" {
                        let bc3 = charCode(s[i + 2])
                        if bc3 < 0 { bc3 = bc3 + 256 }
                        if bc3 != 136 { stillBlock = "0" }
                    }
                    if stillBlock == "1" {
                        runBuf = sbAppend(runBuf, s[i])
                        runBuf = sbAppend(runBuf, s[i + 1])
                        runBuf = sbAppend(runBuf, s[i + 2])
                        i = i + 3
                    }
                }
                richAppendUtf8(rich, sbToString(runBuf), "0x808080", "0")
            } else {
                chunk = sbAppend(chunk, s[i])
                chunkEmpty = "0"
                i = i + 1
            }
        }
    }
    if chunkEmpty == "0" {
        richAppendUtf8(rich, sbToString(chunk), curFg, curBold)
    }
    emit ""
}

// Sanitize UTF-8 multi-byte sequences down to ASCII. RichEdit's
// SendMessageA path converts incoming bytes via the active ANSI
// codepage, which mangles UTF-8 (e.g. `█` 0xE2 0x96 0x88 becomes
// 3 Windows-1252 chars `â–ˆ`). Until SendMessageW + MultiByteToWideChar
// are in the IAT (next compiler rebuild), replace the common block /
// box drawing / degree chars with ASCII so kryofetch's bars render.
func utf8ToAscii(s) {
    let out = sbNew()
    let i = 0
    let n = len(s)
    while i < n {
        let c = charCode(s[i])
        if c < 0 { c = c + 256 }
        if c < 128 {
            out = sbAppend(out, s[i])
            i = i + 1
        } else if c >= 192 && c <= 223 {
            let c2 = 0
            if i + 1 < n {
                c2 = charCode(s[i + 1])
                if c2 < 0 { c2 = c2 + 256 }
            }
            if c == 194 && c2 == 176 {
                out = sbAppend(out, "*")
            } else if c == 194 && c2 == 177 {
                out = sbAppend(out, "+")
            } else {
                out = sbAppend(out, "?")
            }
            i = i + 2
        } else if c >= 224 && c <= 239 {
            let c2 = 0
            let c3 = 0
            if i + 1 < n {
                c2 = charCode(s[i + 1])
                if c2 < 0 { c2 = c2 + 256 }
            }
            if i + 2 < n {
                c3 = charCode(s[i + 2])
                if c3 < 0 { c3 = c3 + 256 }
            }
            if c == 226 && c2 == 150 {
                if c3 == 136 {
                    out = sbAppend(out, "#")
                } else if c3 == 147 {
                    out = sbAppend(out, "=")
                } else if c3 == 146 {
                    out = sbAppend(out, ":")
                } else if c3 == 145 {
                    out = sbAppend(out, ".")
                } else {
                    out = sbAppend(out, "#")
                }
            } else if c == 226 && c2 == 148 {
                if c3 == 128 || c3 == 129 {
                    out = sbAppend(out, "-")
                } else if c3 == 130 || c3 == 131 {
                    out = sbAppend(out, "|")
                } else {
                    out = sbAppend(out, "+")
                }
            } else if c == 226 && c2 == 149 {
                out = sbAppend(out, "+")
            } else {
                out = sbAppend(out, "?")
            }
            i = i + 3
        } else if c >= 240 && c <= 247 {
            out = sbAppend(out, "?")
            i = i + 4
        } else {
            i = i + 1
        }
    }
    emit sbToString(out)
}

// Append text to the RichEdit. ANSI escape sequences are parsed and
// rendered as colored runs; UTF-8 multi-byte chars get converted to
// UTF-16 via utf8ToUtf16Buf and sent through SendMessageW EM_REPLACESEL.
func appendOut(text) {
    let rich = ctxGet("ui.output")
    renderAnsi(rich, text)
    emit ""
}

func appendPrompt() {
    appendOut(ctxGet("brain.prompt"))
    let curLen = richLength()
    ctxSet("promptEnd", curLen + "")
    ctxSet("savedPrefix", richSubstring(0, curLen))
    // Mark [0, curLen) as CFE_PROTECTED at the RichEdit engine level —
    // backspace / delete / paste / typing into this range all bounce off
    // automatically (without the EN_PROTECTED handler subscribed, the
    // engine silently rejects modifications instead of asking). Default
    // char format set to non-protected so new typing in the post-prompt
    // zone remains editable.
    let h = richHwnd()
    SendMessageA(h, "177", "0", curLen + "")           // EM_SETSEL whole prefix
    let cf = bufNew("60")
    bufSetDwordAt(cf, "0", "60")                       // cbSize
    bufSetDwordAt(cf, "4", "16")                       // dwMask = CFM_PROTECTED
    bufSetDwordAt(cf, "8", "16")                       // dwEffects = CFE_PROTECTED
    SendMessageA(h, "1092", "1", ptrToInt(cf))         // EM_SETCHARFORMAT SCF_SELECTION
    bufSetDwordAt(cf, "8", "0")                        // dwEffects = 0
    SendMessageA(h, "1092", "0", ptrToInt(cf))         // EM_SETCHARFORMAT SCF_DEFAULT
    // Park caret at end of prefix (start of typing zone).
    SendMessageA(h, "177", curLen + "", curLen + "")
    emit ""
}


// If the caret has wandered into the prompt zone (mouse click, Home key,
// Ctrl+Home, select-all), push it back to the start of the user's typing
// zone. EM_GETSEL with pointer-out form so we don't lose precision past
// 65k chars. EM_SETSEL = 177, EM_GETSEL = 176.
func clampCaret() {
    let endPos = toInt(ctxGet("promptEnd"))
    if endPos <= 0 { emit "" }
    let h = richHwnd()
    let startBuf = bufNew("4")
    let endBuf = bufNew("4")
    SendMessageA(h, "176", ptrToInt(startBuf), ptrToInt(endBuf))
    let selStart = toInt(bufGetDwordAt(startBuf, "0"))
    let selEnd = toInt(bufGetDwordAt(endBuf, "0"))
    // Active selection — leave it alone so user can highlight output / prompt
    // text for copy. The CFE_PROTECTED format on the prefix range already
    // blocks edits, so the selection is safe to span freely.
    if selStart != selEnd { emit "" }
    if selStart >= endPos { emit "" }
    SendMessageA(h, "177", endPos + "", endPos + "")
    emit ""
}


// Restore the protected prefix if it's been corrupted (e.g. user backspaced
// into the prompt or clicked into history and typed). Wipes everything,
// re-inserts the saved prefix, moves caret to end.
//
// Returns "1" if no restore was needed, "0" if we restored.
func enforcePrefix() {
    let lastLen = toInt(ctxGet("promptEnd"))
    let saved = ctxGet("savedPrefix")
    let curLen = richLength()
    if curLen >= lastLen {
        if richSubstring(0, lastLen) == saved { emit "1" }
    }
    let h = richHwnd()
    SendMessageA(h, "12", "0", ptrToInt(saved))     // WM_SETTEXT
    let endPos = len(saved)
    SendMessageA(h, "177", endPos + "", endPos + "")    // caret at end of prefix
    emit "0"
}


// ── Timer poll ───────────────────────────────────────────────

func onTimer() {
    fitRichToClient()

    // First-tick auto-kryofetch. Set in just-run before guiShow. By the
    // time onTimer fires, the message pump is up and the RichEdit is
    // realized — safe to runCmdLine + append.
    if ctxGet("auto.kryofetch") == "1" {
        ctxSet("auto.kryofetch", "")
        let kfOut = runCmdLine("kryofetch")
        appendOut(kfOut)
        if len(kfOut) == 0 || charCode(kfOut[len(kfOut) - 1]) != 10 {
            appendOut("\n")
        }
        appendPrompt()
    }

    // Re-establish the immutable prefix on every tick. If the user
    // backspaced into the prompt or pasted in front of it, this rewinds
    // the buffer to the snapshot we took at the last appendPrompt().
    if enforcePrefix() == "0" { emit "" }

    let curLen = richLength()
    let lastLen = toInt(ctxGet("promptEnd"))
    if curLen <= lastLen { emit "" }

    // Did the user just press Enter (last char a newline)?
    let tail = richSubstring(curLen - 1, curLen)
    if len(tail) == 0 || charCode(tail[0]) != 10 { emit "" }

    // Extract the command line: everything between the last prompt and
    // the newline (drop the trailing \r if present from CRLF).
    let cmd = richSubstring(lastLen, curLen - 1)
    if len(cmd) > 0 && charCode(cmd[len(cmd) - 1]) == 13 {
        cmd = substring(cmd, 0, len(cmd) - 1)
    }

    if len(cmd) > 0 {
        if cmd == "exit" || cmd == "quit" {
            guiQuit()
            emit ""
        }
        if cmd == "clear" || cmd == "cls" {
            // Clear via EM_SETSEL all + EM_REPLACESEL with ""
            SendMessageA(richHwnd(), "177", "0", "-1")
            SendMessageA(richHwnd(), "194", "0", ptrToInt(""))
            ctxSet("promptEnd", "0")
            appendPrompt()
            emit ""
        }
        let result = runCmdLine(cmd)
        appendOut(result)
        if len(result) == 0 || charCode(result[len(result) - 1]) != 10 {
            appendOut("\n")
        }
    }
    appendPrompt()
    emit ""
}


// ── Resize handler (poll-driven, see fitRichToClient below) ─────
//
// We folded the resize logic into the 100ms onTimer poll. On each tick
// we compare the window's current client area to the last seen size; if
// different, MoveWindow the RichEdit to fill minus an 8px margin.
// Reliable across whatever path WM_SIZE takes through the trampoline.

// Hardcode the margin (don't use a module-level `let` — see
// feedback_module_mutable_globals.md, even reads can be flaky).
func fitRichToClient() {
    let mainRef = ctxGet("ui.window")
    if mainRef == "" { emit "" }
    let mainHwnd = _guiResolveHwnd(mainRef)
    if mainHwnd == "0" { emit "" }
    let rect = bufNew("16")
    GetClientRect(mainHwnd, rect)
    let cw = toInt(bufGetDwordAt(rect, "8"))
    let ch = toInt(bufGetDwordAt(rect, "12"))
    // Anything < 20px is "window hidden / not yet sized" — skip and try again.
    if cw < 20 { emit "" }
    if ch < 20 { emit "" }
    let rw = cw - 16
    let rh = ch - 16
    let richH = _guiResolveHwnd(ctxGet("ui.output"))
    MoveWindow(richH, "8", "8", rw + "", rh + "", "1")
    emit ""
}


// ── Main ─────────────────────────────────────────────────────

just run {
    guiInit()
    let win = guiWindow("brain - Krypton terminal v0.6", 900, 600)
    ctxSet("ui.window", win)

    // Dark title bar (DwmSetWindowAttribute DWMWA_USE_IMMERSIVE_DARK_MODE).
    // Win10 1809+ only; silently no-ops on older builds.
    guiEnableDarkMode()

    // Dark frame around the RichEdit. Slightly darker than the RichEdit's
    // own bg (0x0A1929) so the terminal pane reads as "floating" on the
    // window. Pure-black would be too harsh against the navy text area.
    guiSetWindowBg("0x05101C")

    // Inline RichEdit creation WITHOUT WS_VSCROLL (0x200000) / WS_HSCROLL
    // (0x100000). Even when EM_SHOWSCROLLBAR hides the scrollbar, those
    // styles tell Windows to reserve non-client space for them — and that
    // reserved strip paints in system bg color (white in light theme), no
    // matter what EM_SETBKGNDCOLOR is. Killing the styles outright drops
    // the white inner border. ES_AUTOHSCROLL stays so long lines don't
    // wrap (terminal feel). Style = WS_CHILD|WS_VISIBLE|ES_MULTILINE|
    // ES_AUTOVSCROLL|ES_AUTOHSCROLL|ES_NOHIDESEL|ES_WANTRETURN = 0x500011C4.
    LoadLibraryA("msftedit.dll")
    let pHwnd = _guiResolveHwnd(win)
    let hInst = GetModuleHandleA("0")
    let rich = CreateWindowExA("0", "RICHEDIT50W", "", "1342178756", "8", "8", "884", "584", pHwnd + "", "1001", hInst, "0")
    guiRichReadOnly(rich, "0")          // WRITABLE — the whole point of v0.6
    guiRichSetMonoFont(rich, "Consolas", "11")
    guiRichSetBg(rich, "0x0A1929")
    // Set the DEFAULT char format so user-typed text AND WM_SETTEXT-
    // restored text (used by enforcePrefix) inherit our color. Without
    // this, restored content reverts to RichEdit's default fg (black) —
    // invisible on the dark navy background.
    guiRichSetFgDefault(rich, "0xE0E8F0")
    let richH = _guiResolveHwnd(rich)
    SetWindowTheme(richH, "DarkMode_Explorer", "0")
    ctxSet("ui.output", rich)

    let prompt = makePrompt()
    ctxSet("brain.prompt", prompt)

    appendOut("brain v0.6 - Krypton native terminal\n")
    appendOut("Type a command and press Enter. `exit` to quit. `clear` to clear.\n\n")
    appendPrompt()
    // Flag for onTimer to run kryofetch on the first tick after guiShow.
    ctxSet("auto.kryofetch", "1")

    // WM_SIZE → instant fitRichToClient. Smooth during drag-resize because
    // WM_SIZE fires synchronously per pixel of drag. The 30ms timer below
    // still calls fitRichToClient as backup (and handles Enter detection).
    guiOnResize(funcptr(fitRichToClient))

    // Poll at 30ms (≈33 Hz) — fast enough for resize to look smooth even
    // when WM_SIZE delivery hiccups, slow enough to leave CPU idle.
    guiSetTimer("30", funcptr(onTimer))

    guiShow(win)
    guiRun()
    emit 0
}
