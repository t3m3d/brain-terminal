// kryofetch.k — neofetch-style system info, in Krypton, for Linux.
//
// A "little Krypton here and there": a brain built-in command, written in
// pure Krypton (no C). Reads real Linux system info via exec()/proc and
// renders a Krypton-themed ASCII logo with ANSI colour. Run: kcc -r kryofetch.k
//
// The pre-existing krypton/kryofetch is a STATIC Windows sample; this is the
// working Linux version.

// ── shell capture ─────────────────────────────────────────────────────
// exec() captures stdout (shellRun streams it + returns an exit code).
func cap(cmd) { emit trim(exec(cmd)) }

// value after the first ':' on the first line of text containing key.
func procVal(text, key) {
    let n = lineCount(text)
    let i = 0
    while i < n {
        let ln = getLine(text, i)
        if contains(ln, key) == "1" {
            let c = toInt(indexOf(ln, ":"))
            if c >= 0 { emit trim(substring(ln, c + 1, len(ln))) }
            emit trim(ln)
        }
        i = i + 1
    }
    emit ""
}

// first whitespace-separated token of s (e.g. "14117504 kB" -> "14117504").
func firstTok(s) {
    let i = 0
    let n = len(s)
    while i < n && s[i] != " " { i = i + 1 }
    emit substring(s, 0, i)
}

// integer division rendered as a 1-decimal GB string from a kB value.
func kbToGb(kb) {
    let tenths = (kb * 10) / 1048576    // kB -> GB*10
    let whole = tenths / 10
    let frac = tenths - whole * 10
    emit toStr(whole) + "." + toStr(frac)
}

// a [####....] usage bar, pct 0..100, given inner width.
func bar(pct, width) {
    let fill = (pct * width) / 100
    if fill > width { fill = width }
    let blk = fromCharCode(35)          // '#'
    let dot = fromCharCode(46)          // '.'
    emit "[" + repeat(blk, fill) + repeat(dot, width - fill) + "]"
}

// ── ANSI colour ───────────────────────────────────────────────────────
func col(code) { emit fromCharCode(27) + "[" + code + "m" }

just run {
    let RST = col("0")
    let CY  = col("36")
    let CYB = col("36;1")
    let BL  = col("34;1")
    let GRB = col("32;1")
    let WT  = col("37;1")

    // ── gather facts ──────────────────────────────────────────────────
    let user = cap("whoami")
    let host = cap("cat /proc/sys/kernel/hostname")
    // source os-release so PRETTY_NAME is fully resolved (no key, no quotes).
    let osName = cap(". /etc/os-release 2>/dev/null; printf %s \"$PRETTY_NAME\"")
    if osName == "" { osName = "Linux" }
    let kern = cap("uname -r")
    let arch = cap("uname -m")

    let cpuInfo = exec("cat /proc/cpuinfo")
    let cpu = procVal(cpuInfo, "model name")
    let cores = cap("nproc")

    let mem = exec("cat /proc/meminfo")
    let memTotKb = toInt(firstTok(procVal(mem, "MemTotal")))
    let memAvlKb = toInt(firstTok(procVal(mem, "MemAvailable")))
    let memUsedKb = memTotKb - memAvlKb
    let memPct = 0
    if memTotKb > 0 { memPct = (memUsedKb * 100) / memTotKb }

    let up = exec("cat /proc/uptime")
    let upSec = toInt(firstTok(up))
    let upDays = upSec / 86400
    let upHrs = (upSec - upDays * 86400) / 3600
    let upMin = (upSec - upDays * 86400 - upHrs * 3600) / 60

    let shell = cap("basename \"$SHELL\"")
    if shell == "" { shell = cap("printf %s \"$0\"") }
    let pkgs = cap("(pacman -Qq 2>/dev/null || dpkg -l 2>/dev/null | grep '^ii') | wc -l")

    // ── info column ───────────────────────────────────────────────────
    let memLine = kbToGb(memUsedKb) + " / " + kbToGb(memTotKb) + " GB  "
                + bar(memPct, 18) + "  " + toStr(memPct) + "%"

    let info = ""
    info = info + GRB + "User:   " + RST + user + "@" + host + fromCharCode(10)
    info = info + GRB + "OS:     " + RST + osName + " " + arch + fromCharCode(10)
    info = info + GRB + "Kernel: " + RST + kern + fromCharCode(10)
    info = info + GRB + "CPU:    " + RST + cpu + " (" + cores + "c)" + fromCharCode(10)
    info = info + GRB + "RAM:    " + RST + memLine + fromCharCode(10)
    info = info + GRB + "Uptime: " + RST + toStr(upDays) + "d " + toStr(upHrs) + "h " + toStr(upMin) + "m" + fromCharCode(10)
    info = info + GRB + "Shell:  " + RST + shell + fromCharCode(10)
    info = info + GRB + "Pkgs:   " + RST + pkgs + fromCharCode(10)
    info = info + GRB + "Term:   " + RST + "brain" + fromCharCode(10)

    // ── logo + info, side by side ─────────────────────────────────────
    // A small Krypton crystal. Logo lines on the left, info on the right.
    let B = fromCharCode(92)            // backslash, avoids escape ambiguity
    let logo0 = CYB + "        /" + B + "        " + RST
    let logo1 = CYB + "       /  " + B + "       " + RST
    let logo2 = CY  + "      / /" + B + " " + B + "      " + RST
    let logo3 = CY  + "     / /  " + B + " " + B + "     " + RST
    let logo4 = BL  + "    /_/____" + B + "_" + B + "    " + RST
    let logo5 = BL  + "    " + B + " " + B + "    / /    " + RST
    let logo6 = CY  + "     " + B + " " + B + "  / /     " + RST
    let logo7 = CY  + "      " + B + " " + B + "/ /      " + RST
    let logo8 = CYB + "       " + B + "  /        " + RST
    let logo9 = CYB + "        " + B + "/         " + RST

    let nl = fromCharCode(10)
    kp("")
    // print logo and info rows in parallel (info is \n-delimited)
    let row = 0
    let logoLines = logo0 + nl + logo1 + nl + logo2 + nl + logo3 + nl + logo4 + nl
                  + logo5 + nl + logo6 + nl + logo7 + nl + logo8 + nl + logo9
    let lr = lineCount(logoLines)
    let ir = lineCount(info)
    let rows = lr
    if ir > rows { rows = ir }
    while row < rows {
        let l = ""
        if row < lr { l = getLine(logoLines, row) }
        let r = ""
        if row < ir { r = getLine(info, row) }
        kp(l + "  " + r)
        row = row + 1
    }
    kp("")
    kp("  " + WT + "kryofetch" + RST + " — Krypton on Linux")
    kp("")
}
