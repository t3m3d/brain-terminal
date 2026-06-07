#include "kterm/pty/PTYPlatform.hpp"
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

using namespace kterm::pty;

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
        execl(shellPath.c_str(), shellPath.c_str(), nullptr);
        _exit(1);
    }

    handles.masterFd = masterFd;
    handles.childPid = childPid;
    return handles;
}

void PTYPlatform::resizePTY(long long masterFd, int cols, int rows) {
    struct winsize ws;
    ws.ws_col = cols;
    ws.ws_row = rows;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    ioctl(static_cast<int>(masterFd), TIOCSWINSZ, &ws);
}

long PTYPlatform::readData(long long masterFd, void* buffer, std::size_t bytes) {
    return ::read(static_cast<int>(masterFd), buffer, bytes);
}

long PTYPlatform::writeData(long long masterFd, const void* buffer, std::size_t bytes) {
    return ::write(static_cast<int>(masterFd), buffer, bytes);
}

void PTYPlatform::closePTY(long long masterFd) {
    if (masterFd < 0) return;
    ::close(static_cast<int>(masterFd));
    // Reap any zombie child; non-blocking so we don't stall on a still-running one.
    int status = 0;
    waitpid(-1, &status, WNOHANG);
}
