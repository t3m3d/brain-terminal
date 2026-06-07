#include "brain/theme/Theme.hpp"
#include <fstream>

using namespace brain::theme;

bool Theme::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open())
        return false;

    // Temporary stub until real JSON parsing is added
    setFG(0xFFFFFFFF);
    setBG(0x00000000);

    return true;
}

const ColorScheme& Theme::scheme() const {
    return m_scheme;
}