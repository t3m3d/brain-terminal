#pragma once
#include <string>

namespace brain::font {

class FontManager {
public:
    bool loadFont(const std::string& path);
    int charWidth() const;
    int charHeight() const;

private:
    std::string m_fontPath;
    int m_width = 8;
    int m_height = 16;
};

}