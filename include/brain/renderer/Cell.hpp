#pragma once
#include <cstdint>

namespace brain::renderer {

// Text attribute bit flags (Cell::attrs).
enum CellAttr : uint8_t {
    ATTR_BOLD      = 1,
    ATTR_ITALIC    = 2,
    ATTR_UNDERLINE = 4,
    ATTR_INVERSE   = 8,
    ATTR_STRIKE    = 16,   // SGR 9 / 29   crossed-out
    ATTR_DIM       = 32,   // SGR 2 / 22   faint / reduced intensity
};

struct Cell {
    uint32_t ch = ' ';   // Unicode codepoint
    uint32_t fg = 0xFFFFFFFF;
    uint32_t bg = 0x00000000;
    uint8_t  attrs = 0;  // CellAttr bit flags
    uint8_t  pad = 0;
    uint16_t link = 0;   // OSC 8 hyperlink id, 0 = none. Resolved via
                         // Terminal::linkUri(id). 16 bits keeps Cell at
                         // 16 B alignment — 65 535 distinct links per
                         // session is far more than any real workload.
};

}