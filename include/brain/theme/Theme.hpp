#pragma once
#include <string>
#include <cstdint>
#include <unordered_map>

namespace brain::theme {

struct ColorScheme {
    uint32_t fg = 0xFFFFFFFF;
    uint32_t bg = 0x00000000;
    std::unordered_map<int, uint32_t> palette;
};

class Theme {
public:
    bool loadFromFile(const std::string& path);

    const ColorScheme& scheme() const;

    void setFG(uint32_t color) { m_scheme.fg = color; }
    void setBG(uint32_t color) { m_scheme.bg = color; }
    void setPaletteColor(int index, uint32_t color) { m_scheme.palette[index] = color; }

private:
    ColorScheme m_scheme;
};

}