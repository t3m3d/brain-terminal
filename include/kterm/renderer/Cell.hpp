#pragma once
#include <cstdint>

namespace kterm::renderer {

struct Cell {
    uint32_t ch = ' ';   // Unicode codepoint (was char; widened for UTF-8)
    uint32_t fg = 0xFFFFFFFF;
    uint32_t bg = 0x00000000;
};

}