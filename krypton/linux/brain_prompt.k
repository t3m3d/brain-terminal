// brain_prompt.k — a fast shell prompt in Krypton (user@host, cwd, git, exit-status, OSC 133).
//   kcc -o brain_prompt brain_prompt.k   then   PS1='$(brain_prompt $?)'

func col(code) { emit fromCharCode(27) + "[" + code + "m" }

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
    let CY  = col("36;1")
    let BL  = col("34;1")
    let YL  = col("33;1")
    let RD  = col("31;1")
    let GR  = col("32;1")

    let exitCode = arg("0")

    let user = trim(exec("whoami"))
    let host = trim(exec("cat /proc/sys/kernel/hostname"))

    let cwd = trim(exec("pwd"))
    let home = trim(exec("printf %s \"$HOME\""))
    if len(home) > 0 && len(cwd) >= len(home) {
        if substring(cwd, 0, len(home)) == home {
            cwd = "~" + substring(cwd, len(home), len(cwd))
        }
    }

    let branch = trim(exec("git rev-parse --abbrev-ref HEAD 2>/dev/null"))
    let gitPart = ""
    if branch != "" {
        let dirty = trim(exec("git status --porcelain 2>/dev/null"))
        let mark = ""
        if dirty != "" { mark = RD + "*" + YL }
        gitPart = " " + YL + "(" + branch + mark + ")" + RST
    }

    let glyphCol = GR
    if exitCode != "" && exitCode != "0" { glyphCol = RD }
    let glyph = glyphCol + utf8(10095) + RST

    let osc133 = fromCharCode(27) + "]133;A" + fromCharCode(27) + fromCharCode(92)

    kp(osc133 + CY + user + "@" + host + RST
       + " " + BL + cwd + RST
       + gitPart
       + " " + glyph + " ")
}
