#pragma once
#include <cstdint>

namespace kterm::renderer {

// Text attribute bit flags (Cell::attrs).
enum CellAttr : uint8_t {
    ATTR_BOLD      = 1,
    ATTR_ITALIC    = 2,
    ATTR_UNDERLINE = 4,
    ATTR_INVERSE   = 8,
};

struct Cell {
    uint32_t ch = ' ';   // Unicode codepoint (was char; widened for UTF-8)
    uint32_t fg = 0xFFFFFFFF;
    uint32_t bg = 0x00000000;
    uint8_t  attrs = 0;  // CellAttr bit flags
};

}