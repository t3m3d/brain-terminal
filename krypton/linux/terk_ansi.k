// terk_ansi.k — ANSI / VT escape-sequence parser, in Krypton.
//
// Faithful port of brain-terminal/src/parser/AnsiParser.cpp. Pure logic:
// no syscalls, no GUI. This is the first piece of the Linux Krypton terk —
// the terminal core that turns a raw PTY byte stream into a list of typed
// events (text runs, cursor moves, clears, SGR colour/attrs, modes, OSC).
//
// ansiParse(input) -> newline-separated event log, one event per line:
//   TEXT <text>            printable run (control bytes shown \n \r \t \e)
//   CURSOR_UP|DOWN|FWD|BACK <n>
//   SET_CURSOR <row> <col>
//   CLEAR_SCREEN <mode>    0=->end 1=start-> 2/3=all
//   CLEAR_LINE <mode>
//   SGR <params>           ;-separated SGR list (colours + attrs), "" -> 0
//   SET_MODE|RESET_MODE <n> <priv>   priv=1 for DEC private (ESC[?..)
//   OSC <payload>          shell-integration etc.
//   UNKNOWN <final>
//
// Note: v1 assumes complete sequences within one input chunk. Incremental
// buffering across feeds (split escapes) is a follow-up.

// ── small char helpers ────────────────────────────────────────────────

func isAlphaCh(c) {
    let n = charCode(c)
    if n >= 65 && n <= 90 { emit "1" }    // A-Z (CSI final bytes)
    if n >= 97 && n <= 122 { emit "1" }   // a-z
    emit "0"
}

// nth ';'-delimited field of s, or "" if absent. Hand-rolled because the
// native splitBy/list path is unreliable on Linux (LINUX_RELEASE_TODO).
func nthField(s, sep, idx) {
    let cur = 0
    let start = 0
    let i = 0
    let n = len(s)
    while i < n {
        if s[i] == sep {
            if cur == idx { emit substring(s, start, i) }
            cur = cur + 1
            start = i + 1
        }
        i = i + 1
    }
    if cur == idx { emit substring(s, start, n) }
    emit ""
}

// CSI integer param idx with default def (matches the C++ p(i,def) lambda).
func csiNum(params, idx, def) {
    let f = nthField(params, ";", idx)
    if f == "" { emit toStr(def) }
    emit toStr(toInt(f))
}

// escape control bytes in a text run so the event log stays one-line.
func escText(s) {
    let bsl = fromCharCode(92)   // backslash, avoids string-literal escape ambiguity
    let sb = sbNew()
    let i = 0
    while i < len(s) {
        let c = s[i]
        let n = charCode(c)
        if n == 10 { sb = sbAppend(sb, bsl + "n") }
        else if n == 13 { sb = sbAppend(sb, bsl + "r") }
        else if n == 9 { sb = sbAppend(sb, bsl + "t") }
        else if n == 27 { sb = sbAppend(sb, bsl + "e") }
        else { sb = sbAppend(sb, c) }
        i = i + 1
    }
    emit sbToString(sb)
}

// ── CSI dispatch ──────────────────────────────────────────────────────
// seq is the full "ESC [ ... <final>" string. Returns one event line.

func parseCSI(seq) {
    let n = len(seq)
    let final = seq[n - 1]
    let params = substring(seq, 2, n - 1)   // between '[' and the final byte
    let priv = "0"
    if len(params) > 0 {
        let c0 = params[0]
        if c0 == "?" || c0 == ">" || c0 == "=" || c0 == "<" {
            if c0 == "?" { priv = "1" }
            params = substring(params, 1, len(params))
        }
    }
    if final == "A" { emit "CURSOR_UP " + csiNum(params, 0, 1) }
    if final == "B" { emit "CURSOR_DOWN " + csiNum(params, 0, 1) }
    if final == "C" { emit "CURSOR_FWD " + csiNum(params, 0, 1) }
    if final == "D" { emit "CURSOR_BACK " + csiNum(params, 0, 1) }
    if final == "H" || final == "f" {
        emit "SET_CURSOR " + csiNum(params, 0, 1) + " " + csiNum(params, 1, 1)
    }
    if final == "J" { emit "CLEAR_SCREEN " + csiNum(params, 0, 0) }
    if final == "K" { emit "CLEAR_LINE " + csiNum(params, 0, 0) }
    if final == "m" {
        if params == "" { emit "SGR 0" }
        emit "SGR " + params
    }
    if final == "h" { emit "SET_MODE " + csiNum(params, 0, 0) + " " + priv }
    if final == "l" { emit "RESET_MODE " + csiNum(params, 0, 0) + " " + priv }
    emit "UNKNOWN " + final
}

// ── main parse loop ───────────────────────────────────────────────────

func ansiParse(input) {
    let ESC = fromCharCode(27)
    let BEL = fromCharCode(7)
    let BSL = fromCharCode(92)
    let nl  = fromCharCode(10)
    let ev = sbNew()
    let n = len(input)
    let pos = 0
    let textStart = 0
    while pos < n {
        if input[pos] != ESC {
            pos = pos + 1
            continue
        }
        // flush pending text run [textStart, pos)
        if pos > textStart {
            ev = sbAppend(ev, "TEXT " + escText(substring(input, textStart, pos)) + nl)
        }
        if pos + 1 >= n { textStart = pos  pos = n  continue }
        let c1 = input[pos + 1]

        if c1 == "]" {
            // OSC: ESC ] <payload> (BEL | ESC '\')
            let i = pos + 2
            let termLen = 0
            while i < n {
                if input[i] == BEL { termLen = 1  break }
                if input[i] == ESC && i + 1 < n && input[i + 1] == BSL { termLen = 2  break }
                i = i + 1
            }
            if i >= n { textStart = pos  pos = n  continue }   // unterminated -> leftover
            ev = sbAppend(ev, "OSC " + escText(substring(input, pos + 2, i)) + nl)
            pos = i + termLen
            textStart = pos
            continue
        }

        if c1 != "[" {
            pos = pos + 2          // ESC <other> — skip
            textStart = pos
            continue
        }

        // CSI: scan to the final (alpha) byte
        let end = pos + 2
        while end < n && isAlphaCh(input[end]) == "0" {
            end = end + 1
        }
        if end >= n { textStart = pos  pos = n  continue }      // unterminated CSI
        ev = sbAppend(ev, parseCSI(substring(input, pos, end + 1)) + nl)
        pos = end + 1
        textStart = pos
    }
    if textStart < n {
        ev = sbAppend(ev, "TEXT " + escText(substring(input, textStart, n)) + nl)
    }
    emit sbToString(ev)
}

// ── self-test ─────────────────────────────────────────────────────────

just run {
    let ESC = fromCharCode(27)
    let BEL = fromCharCode(7)
    let input = "hello " + ESC + "[31m" + "red" + ESC + "[0m"
              + ESC + "[2J" + ESC + "]0;mytitle" + BEL + "done" + ESC + "[1;5H"
              + ESC + "[?25l" + ESC + "[38;5;200m" + "x" + ESC + "[A"
    kp("=== terk ANSI parser — event trace ===")
    kp(ansiParse(input))
}
