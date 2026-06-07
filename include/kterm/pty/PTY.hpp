#pragma once
#include <string>
#include <functional>
#include <vector>

namespace kterm::pty {

class PTY {
public:
    using OutputCallback = std::function<void(const std::vector<char>&)>;

    PTY();
    ~PTY();

    bool spawnShell(const std::string& shellPath = "/bin/bash");
    void setOutputCallback(OutputCallback cb);

    bool writeInput(const std::string& data);
    void resize(int cols, int rows);

private:
    // Opaque handle from PTYPlatform::createPTY(). On Linux this is the
    // POSIX master PTY fd; on Windows it's a token into the ConPTY map.
    // Always go through PTYPlatform for I/O — never POSIX read/write/close.
    long long m_masterFd;
    OutputCallback m_callback;

    void readLoop();
};

}