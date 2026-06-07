#include "brain/pty/PTYPlatform.hpp"
#include <util.h>          // forkpty (macOS lives in <util.h>, not <pty.h>)
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <cstdlib>

using namespace brain::pty;

PTYHandles PTYPlatform::createPTY(const std::string& shellPath, int cols, int rows) {
    PTYHandles handles;

    struct winsize ws;
    ws.ws_col = cols;
    ws.ws_row = rows;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    int masterFd;
    int childPid = forkpty(&masterFd, nullptr, nullptr, &ws);

    if (childPid < 0)
        return handles;

    if (childPid == 0) {
        // Child: give programs a sane terminal type, then become the shell.
        // Launch as a login shell (argv[0] prefixed with '-') so the user's
        // profile (PATH, prompt, etc.) is sourced — the macOS default.
        setenv("TERM", "xterm-256color", 1);
        // Identify ourselves so fetch tools don't report the inherited terminal
        // (e.g. whatever launched the .app). Overwrite any inherited value.
        setenv("TERM_PROGRAM", "brain", 1);
        unsetenv("TERM_PROGRAM_VERSION");

        std::string argv0 = "-";
        argv0 += shellPath;  // e.g. "-/bin/zsh"
        execl(shellPath.c_str(), argv0.c_str(), (char*)nullptr);
        _exit(1);
    }

    handles.masterFd = masterFd;
    handles.childPid = childPid;
    return handles;
}

void PTYPlatform::resizePTY(int masterFd, int cols, int rows) {
    struct winsize ws;
    ws.ws_col = cols;
    ws.ws_row = rows;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    ioctl(masterFd, TIOCSWINSZ, &ws);
}
