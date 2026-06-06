# kterm shell integration — OSC 133 command blocks (FinalTerm protocol).
#
# Tells kterm where each command's prompt + output begin and end, and whether
# the command succeeded, so kterm can draw the colored gutter bars:
#   green = exit 0, red = nonzero exit, amber = still running.
#
# Install: add this line to the END of your ~/.zshrc (after p10k / oh-my-zsh):
#   source /path/to/terk/platform/macos/shell-integration.zsh

[[ -n "$KTERM_SHELL_INTEGRATION" ]] && return
typeset -g KTERM_SHELL_INTEGRATION=1

autoload -Uz add-zsh-hook

# Before each prompt: close the previous command's block (with its exit code),
# then open a new block at the prompt line (status: running).
__kterm_precmd() {
  local ret=$?
  printf '\e]133;D;%s\a' "$ret"
  printf '\e]133;A\a'
}

# Just before a command runs: mark the start of its output.
__kterm_preexec() {
  printf '\e]133;C\a'
}

add-zsh-hook precmd  __kterm_precmd
add-zsh-hook preexec __kterm_preexec
