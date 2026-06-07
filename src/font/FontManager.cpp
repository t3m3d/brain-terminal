#include "brain/font/FontManager.hpp"

using namespace brain::font;

bool FontManager::loadFont(const std::string& path) {
    m_fontPath = path;
    // Load font file, set metrics
    return true;
}

int FontManager::charWidth() const {
    return m_width;
}

int FontManager::charHeight() const {
    return m_height;
}