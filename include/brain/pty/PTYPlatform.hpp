#pragma once
#include <cstddef>
#include <string>

namespace brain::pty {

// PTY handles returned by createPTY().
//
// Linux: masterFd is the POSIX master PTY fd. childPid is the forked
// child shell's pid.
//
// Windows: ConPTY has a HPCON + separate input/output pipes + child
// PROCESS_INFORMATION. We can't squeeze all of that into two ints, so
// on Windows `masterFd` is treated as an OPAQUE TOKEN that
// PTYPlatform::readData / writeData / closePTY look up via a
// process-wide map. Callers should NOT pass this to POSIX read()/
// write()/close() on Windows — always go through PTYPlatform.
struct PTYHandles {
    long long masterFd = -1;   // POSIX fd on Linux; opaque token on Windows.
    long long childPid = -1;   // POSIX pid on Linux; child process id on Windows.
};

class PTYPlatform {
public:
    // Spawn `shellPath` attached to a fresh PTY sized `cols` x `rows`.
    // Returns {-1, -1} on failure.
    static PTYHandles createPTY(const std::string& shellPath, int cols, int rows);

    // Resize the PTY's window dimensions.
    static void resizePTY(long long masterFd, int cols, int rows);

    // Bidirectional I/O against the master side.
    //
    // Returns the byte count read/written. <0 = error, 0 = EOF (child
    // exited). On Windows these route through the per-process map of
    // input + output pipes keyed by `masterFd`; on Linux they're thin
    // POSIX read()/write() wrappers.
    static long readData(long long masterFd, void* buffer, std::size_t bytes);
    static long writeData(long long masterFd, const void* buffer, std::size_t bytes);

    // Tear down the PTY + reap the child. Idempotent for unknown fds.
    static void closePTY(long long masterFd);
};

}
