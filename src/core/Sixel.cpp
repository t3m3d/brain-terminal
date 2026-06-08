#include "brain/core/Sixel.hpp"
#include <array>
#include <cstdlib>

using namespace brain::core;

namespace {

constexpr int kMaxDim = 10000;   // guard against malformed huge images

// VT340 default 16-colour palette (0xAARRGGBB, opaque).
constexpr uint32_t kDefaultPalette[16] = {
    0xFF000000, 0xFF3333CC, 0xFFCC2121, 0xFF33CC33,
    0xFFCC33CC, 0xFF33CCCC, 0xFFCCCC33, 0xFF878787,
    0xFF424242, 0xFF545487, 0xFF873C3C, 0xFF548754,
    0xFF87547D, 0xFF548787, 0xFF878754, 0xFFCCCCCC,
};

inline uint32_t hslToArgb(int h, int s, int l) {
    double H = (h % 360) / 360.0, S = s / 100.0, L = l / 100.0;
    auto hue = [](double p, double q, double t) {
        if (t < 0) t += 1;
        if (t > 1) t -= 1;
        if (t < 1.0/6) return p + (q - p) * 6 * t;
        if (t < 1.0/2) return q;
        if (t < 2.0/3) return p + (q - p) * (2.0/3 - t) * 6;
        return p;
    };
    double r, g, b;
    if (S == 0) { r = g = b = L; }
    else {
        double q = L < 0.5 ? L * (1 + S) : L + S - L * S;
        double p = 2 * L - q;
        r = hue(p, q, H + 1.0/3); g = hue(p, q, H); b = hue(p, q, H - 1.0/3);
    }
    return 0xFF000000u | ((uint32_t)(r * 255) << 16) | ((uint32_t)(g * 255) << 8)
         | (uint32_t)(b * 255);
}

// Read a run of digits starting at i, return the value and advance i.
int readInt(const std::string& s, size_t& i) {
    int v = 0; bool any = false;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') { v = v * 10 + (s[i] - '0'); ++i; any = true; }
    return any ? v : -1;
}

} // namespace

SixelImage brain::core::decodeSixel(const std::string& payload) {
    SixelImage out;
    size_t q = payload.find('q');
    if (q == std::string::npos) return out;          // not a Sixel DCS
    const std::string data = payload.substr(q + 1);

    // ---- Pass 1: measure geometry (colours ignored) ----
    int x = 0, band = 0, maxX = 0, maxBand = 0;
    for (size_t i = 0; i < data.size();) {
        char c = data[i];
        if (c == '#' || c == '"') {                  // colour / raster attrs: skip params
            ++i;
            while (i < data.size() && ((data[i] >= '0' && data[i] <= '9') || data[i] == ';')) ++i;
        } else if (c == '!') {                        // repeat: !Pn <sixel>
            ++i; int n = readInt(data, i);
            if (n < 1) n = 1;
            if (i < data.size() && data[i] >= '?' && data[i] <= '~') { x += n; ++i; }
            if (x > maxX) maxX = x;
        } else if (c == '$') { x = 0; ++i; }
        else if (c == '-')   { x = 0; ++band; if (band > maxBand) maxBand = band; ++i; }
        else if (c >= '?' && c <= '~') { ++x; if (x > maxX) maxX = x; ++i; }
        else ++i;                                     // CR/LF/space/other
        if (band > maxBand) maxBand = band;
    }
    int w = maxX, h = (maxBand + 1) * 6;
    if (w <= 0 || h <= 0 || w > kMaxDim || h > kMaxDim) return out;

    std::vector<uint32_t> pix((size_t)w * h, 0x00000000u);  // transparent

    // ---- Pass 2: fill, tracking colours ----
    std::array<uint32_t, 256> pal{};
    for (int k = 0; k < 256; ++k) pal[k] = kDefaultPalette[k % 16];
    int color = 0;
    x = 0; band = 0;

    auto plot = [&](int px) {
        int v = px - '?';
        for (int bit = 0; bit < 6; ++bit) {
            if (v & (1 << bit)) {
                int yy = band * 6 + bit;
                if (x >= 0 && x < w && yy >= 0 && yy < h)
                    pix[(size_t)yy * w + x] = pal[color & 0xFF];
            }
        }
    };

    for (size_t i = 0; i < data.size();) {
        char c = data[i];
        if (c == '#') {
            ++i; int pc = readInt(data, i);
            if (pc < 0) continue;
            if (i < data.size() && data[i] == ';') {
                ++i; int pu = readInt(data, i);
                int p1 = (i < data.size() && data[i] == ';') ? (++i, readInt(data, i)) : -1;
                int p2 = (i < data.size() && data[i] == ';') ? (++i, readInt(data, i)) : -1;
                int p3 = (i < data.size() && data[i] == ';') ? (++i, readInt(data, i)) : -1;
                if (pu == 2 && p1 >= 0 && p2 >= 0 && p3 >= 0)        // RGB 0..100
                    pal[pc & 0xFF] = 0xFF000000u | ((uint32_t)(p1 * 255 / 100) << 16)
                                   | ((uint32_t)(p2 * 255 / 100) << 8) | (uint32_t)(p3 * 255 / 100);
                else if (pu == 1 && p1 >= 0 && p2 >= 0 && p3 >= 0)   // HSL
                    pal[pc & 0xFF] = hslToArgb(p1, p2, p3);
            }
            color = pc;
        } else if (c == '"') {                        // raster attrs: skip
            ++i;
            while (i < data.size() && ((data[i] >= '0' && data[i] <= '9') || data[i] == ';')) ++i;
        } else if (c == '!') {
            ++i; int n = readInt(data, i);
            if (n < 1) n = 1;
            if (i < data.size() && data[i] >= '?' && data[i] <= '~') {
                for (int r = 0; r < n; ++r) { plot(data[i]); ++x; }
                ++i;
            }
        } else if (c == '$') { x = 0; ++i; }
        else if (c == '-')   { x = 0; ++band; ++i; }
        else if (c >= '?' && c <= '~') { plot(c); ++x; ++i; }
        else ++i;
    }

    out.w = w; out.h = h; out.argb = std::move(pix);
    return out;
}
