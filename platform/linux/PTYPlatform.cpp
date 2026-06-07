#include "brain/pty/PTYPlatform.hpp"
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <cstdlib>
#include <cstdio>
#include <sstream>
#include <string>

using namespace brain::pty;

namespace {

std::string kryptonResolveShell() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    std::string cfgDir = (xdg && *xdg) ? std::string(xdg)
                       : (home ? std::string(home) + "/.config" : std::string());
    if (cfgDir.empty()) return "";
    std::string script = cfgDir + "/brain-terminal/setup.ks";
    if (access(script.c_str(), R_OK) != 0) return "";

    std::string runner;
    if (std::system("command -v kr >/dev/null 2>&1") == 0)       runner = "kr ";
    else if (std::system("command -v kcc >/dev/null 2>&1") == 0) runner = "kcc -r ";
    else return "";

    std::string q = "'";
    for (char c : script) { if (c == '\'') q += "'\\''"; else q += c; }
    q += "'";
    std::string cmd = runner + q + " 2>/dev/null";

    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return "";
    std::string acc; char buf[4096];
    while (std::fgets(buf, sizeof buf, p)) acc += buf;
    pclose(p);

    std::string last, line;
    std::istringstream ss(acc);
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        last = line;
    }
    return last;
}

std::string resolveShell(const std::string& shellPath) {
    std::string kr = kryptonResolveShell();
    if (!kr.empty()) return kr;
    if (!shellPath.empty() && shellPath[0] == '/' && access(shellPath.c_str(), X_OK) == 0)
        return shellPath;
    const char* sh = std::getenv("SHELL");
    if (sh && *sh && access(sh, X_OK) == 0) return sh;
    return "/bin/bash";
}

}  // namespace

PTYHandles PTYPlatform::createPTY(const std::string& shellPath, int cols, int rows) {
    PTYHandles handles;

    struct winsize ws;
    ws.ws_col = cols;
    ws.ws_row = rows;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    std::string shell = resolveShell(shellPath);

    int masterFd;
    int childPid = forkpty(&masterFd, nullptr, nullptr, &ws);

    if (childPid < 0)
        return handles;

    if (childPid == 0) {
        if (const char* home = getenv("HOME")) {
            (void)chdir(home);
        }
        execlp(shell.c_str(), shell.c_str(), nullptr);
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
    int status = 0;
    waitpid(-1, &status, WNOHANG);
}
