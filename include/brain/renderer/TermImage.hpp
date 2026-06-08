#pragma once
#include <cstdint>
#include <vector>

namespace brain::renderer {

// A decoded inline image (Sixel/etc.) anchored to a grid position. anchorAbs is
// the ABSOLUTE line it sits on (grid.absScroll()+row at placement) so it scrolls
// with the text. argb is row-major 0xAARRGGBB, wpx*hpx entries.
struct TermImage {
    long long anchorAbs = 0;
    int col  = 0;
    int wpx  = 0;
    int hpx  = 0;
    std::vector<uint32_t> argb;
};

} // namespace brain::renderer
