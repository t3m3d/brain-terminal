#pragma once
#include <string>
#include <cstdint>
#include <vector>

namespace brain::core {

struct SixelImage {
    int w = 0;
    int h = 0;
    std::vector<uint32_t> argb;   // row-major 0xAARRGGBB, w*h entries
};

// Decode a Sixel DCS payload (everything between "ESC P" and ST, i.e. the
// "P1;P2;P3q<data>" string). Returns an empty image (w==0) if there's no
// Sixel data. Pixels never written stay transparent (0x00000000).
SixelImage decodeSixel(const std::string& payload);

} // namespace brain::core
