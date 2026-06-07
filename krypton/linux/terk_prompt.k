// terk_prompt.k — a fast shell prompt, in Krypton, for Linux.
//
// Another "little Krypton here and there" terk built-in: a native prompt
// generator (like starship/powerline, but Krypton + no deps). Shows
// user@host, ~-shortened cwd, git branch + dirty flag, and an exit-status
// coloured prompt glyph. Emits an OSC 133 prompt-start marker so it lights
// up terk's command-block UI.
//
// Build + wire into your shell:
//   kcc -o terk_prompt terk_prompt.k
//   # bash:  PS1='$(terk_prompt $?)'
//   # the $? is passed as arg 0 -> colours the glyph green/red.

func col(code) { emit fromCharCode(27) + "[" + code + "m" }

// UTF-8 bytes for a BMP codepoint (so we can use nice glyphs without
// relying on Krypton string-literal unicode).
func utf8(cp) {
    if cp < 128 { emit fromCharCode(cp) }
    if cp < 2048 {
        emit fromCharCode(192 + cp / 64) + fromCharCode(128 + cp % 64)
    }
    let b1 = 224 + cp / 4096
    let b2 = 128 + (cp / 64) % 64
    let b3 = 128 + cp % 64
    emit fromCharCode(b1) + fromCharCode(b2) + fromCharCode(b3)
}

just run {
    let RST = col("0")
    let CY  = col("36;1")    // user@host
    let BL  = col("34;1")    // cwd
    let YL  = col("33;1")    // git branch
    let RD  = col("31;1")    // dirty / error
    let GR  = col("32;1")    // ok

    let exitCode = arg("0")          // shell passes $? as arg 0 (when compiled)

    let user = trim(exec("whoami"))
    let host = trim(exec("cat /proc/sys/kernel/hostname"))

    // cwd, with $HOME collapsed to ~
    let cwd = trim(exec("pwd"))
    let home = trim(exec("printf %s \"$HOME\""))
    if len(home) > 0 && len(cwd) >= len(home) {
        if substring(cwd, 0, len(home)) == home {
            cwd = "~" + substring(cwd, len(home), len(cwd))
        }
    }

    // git branch + dirty
    let branch = trim(exec("git rev-parse --abbrev-ref HEAD 2>/dev/null"))
    let gitPart = ""
    if branch != "" {
        let dirty = trim(exec("git status --porcelain 2>/dev/null"))
        let mark = ""
        if dirty != "" { mark = RD + "*" + YL }
        gitPart = " " + YL + "(" + branch + mark + ")" + RST
    }

    // exit-status coloured glyph (heavy angle quote)
    let glyphCol = GR
    if exitCode != "" && exitCode != "0" { glyphCol = RD }
    let glyph = glyphCol + utf8(10095) + RST    // U+276F  >

    let osc133 = fromCharCode(27) + "]133;A" + fromCharCode(27) + fromCharCode(92)

    // single line: user@host  cwd  (branch*)  >
    kp(osc133 + CY + user + "@" + host + RST
       + " " + BL + cwd + RST
       + gitPart
       + " " + glyph + " ")
}
