// brain-terminal — Krypton shell picker
//
// brain-terminal looks for this script at startup. If `kr.exe` (the
// Krypton 2.3+ KryptScript runner) is on PATH and this file exists at
//   %APPDATA%\brain-terminal\setup.ks            (Windows)
//   $XDG_CONFIG_HOME/brain-terminal/setup.ks      (Linux / macOS)
// the terminal runs it and uses the LAST LINE of stdout as the shell
// command. Anything else printed is ignored, so feel free to log
// reasoning for your future self.
//
// Bail out (print nothing on the last line, or empty string) to fall
// back to the terminal's built-in default (cmd.exe on Windows,
// /bin/bash on Linux).
//
// Examples below pick a different shell depending on context.

import "k:env"
import "k:fsx"

just run {
    let os = env("OS")          // Windows sets OS=Windows_NT; POSIX unsets it
    let cwd = env("PWD")
    if cwd == "" { cwd = env("USERPROFILE") }

    // If we're inside a Krypton repo, drop into the KryptScript REPL —
    // makes `import "k:foo"` etc. one keystroke away.
    if exists(cwd + "/compiler/compile.k") == "1" {
        if exists(cwd + "/kcc.ks") == "1" {
            kp("# Krypton repo detected; using kr REPL")
            if os == "Windows_NT" { kp("kr.exe") }
            else                  { kp("kr") }
            exit("0")
        }
    }

    // If `pwsh` is installed and we're on Windows, prefer it over cmd.
    if os == "Windows_NT" {
        if exists("C:/Program Files/PowerShell/7/pwsh.exe") == "1" {
            kp("# PowerShell 7 found")
            kp("C:/Program Files/PowerShell/7/pwsh.exe")
            exit("0")
        }
        kp("# Falling back to cmd")
        kp("cmd.exe")
        exit("0")
    }

    // POSIX: pick zsh if installed, else bash.
    if exists("/bin/zsh") == "1" { kp("/bin/zsh")  exit("0") }
    kp("/bin/bash")
}
