#include "brain/pty/PTYPlatform.hpp"

#if defined(__APPLE__)
#include "../../platform/macos/PTYPlatform.cpp"
#elif defined(__linux__)
#include "../../platform/linux/PTYPlatform.cpp"
#elif defined(_WIN32)
#include "../../platform/windows/PTYPlatform.cpp"
#else
#error "Unsupported platform"
#endif