#include "brain/Config.hpp"
#include <fstream>
#include <sstream>
#include <cctype>
#include <cstdlib>
#include <vector>

using namespace brain;

namespace {

// Resolve the `theme` value. A path (contains '/' or ends in .json) is used
// as-is; a bare name like "gruvbox-dark" is resolved to a bundled theme JSON,
// searching the user dir then the dev tree then the install prefix.
static std::string resolveThemePath(const std::string& v) {
    if (v.empty()) return v;
    bool looksLikePath = v.find('/') != std::string::npos
                      || (v.size() > 5 && v.substr(v.size() - 5) == ".json");
    if (looksLikePath) return v;

    std::vector<std::string> dirs;
    if (const char* home = std::getenv("HOME"))
        dirs.push_back(std::string(home) + "/.config/brain/themes/");
    dirs.push_back("resources/themes/");
    dirs.push_back("/usr/share/brain/themes/");
    dirs.push_back("/usr/local/share/brain/themes/");
    for (const std::string& d : dirs) {
        std::string p = d + v + ".json";
        std::ifstream f(p);
        if (f.good()) return p;
    }
    return "resources/themes/" + v + ".json";
}

// Parse a hex colour "#rrggbb" / "#aarrggbb" (also without '#') into
// 0xAARRGGBB. 6-digit forms get an opaque alpha. Returns 0 (= unset) on junk.
static uint32_t parseColor(const std::string& s) {
    std::string h = s;
    if (!h.empty() && h[0] == '#') h.erase(0, 1);
    if (h.size() != 6 && h.size() != 8) return 0;
    uint32_t v = 0;
    for (char c : h) {
        int d;
        if      (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return 0;
        v = (v << 4) | static_cast<uint32_t>(d);
    }
    if (h.size() == 6) v |= 0xFF000000u;   // opaque
    if (v == 0) v = 0xFF000000u;           // pure black -> keep non-zero (alpha)
    return v;
}

// Per-platform sensible defaults.
//
// Font: pick a real monospace family BY NAME so Qt's font matcher doesn't
// fall back to a proportional face. "Monospace" is just a hint and Qt
// historically falls back to Microsoft Sans Serif on Windows when no
// installed family matches that name — that's the bug behind the
// "M i c r o s o f t" widely-spaced rendering. Cascadia Mono ships with
// Windows 10/11; Consolas is a guaranteed fallback.
//
// Shell: brain's PTY backend resolves "/bin/bash" to cmd.exe on Windows,
// so we keep the cross-platform default. Users override via the conf file.
const char* defaultFontFamily() {
#if defined(_WIN32)
    return "Cascadia Mono";
#elif defined(__APPLE__)
    return "Menlo";
#else
    return "Monospace";
#endif
}

const char* defaultShell() {
    return "/bin/bash";   // PTYPlatform::resolveShell maps to cmd.exe on Windows
}

} // namespace

Config Config::defaults() {
    Config c;
    c.m_shell      = defaultShell();
    c.m_themePath  = "resources/themes/default.json";
    c.m_fontFamily = defaultFontFamily();
    return c;
}

// ------------------------------------------------------------
// find the correct config file to load
// ------------------------------------------------------------
static std::string findConfigPath(const std::string& overridePath) {
    if (!overridePath.empty()) {
        std::ifstream f(overridePath);
        if (f.good()) return overridePath;
    }

    // User config (~/.config/brain/brain.conf or %APPDATA%\brain\brain.conf)
#ifndef _WIN32
    if (const char* home = getenv("HOME")) {
        std::string userPath = std::string(home) + "/.config/brain/brain.conf";
        std::ifstream f(userPath);
        if (f.good()) return userPath;
    }
#else
    if (const char* appdata = getenv("APPDATA")) {
        std::string userPath = std::string(appdata) + "\\brain\\brain.conf";
        std::ifstream f(userPath);
        if (f.good()) return userPath;
    }
#endif

    // Local config (./brain.conf)
    {
        std::ifstream f("brain.conf");
        if (f.good()) return "brain.conf";
    }

    // Built-in default config (relative to launcher CWD, used when no
    // user/local override exists).
    return "resources/config/brain.conf";
}

// ------------------------------------------------------------
// Load config from file (simple key=value parser, # comments)
// ------------------------------------------------------------
Config Config::load(const std::string& path) {
    std::string finalPath = findConfigPath(path);
    std::ifstream file(finalPath);

    Config c = defaults();
    if (!file.is_open())
        return c;
    c.m_sourcePath = finalPath;

    auto trim = [](std::string& s) {
        s.erase(0, s.find_first_not_of(" \t"));
        s.erase(s.find_last_not_of(" \t\r\n") + 1);
    };

    auto unquote = [](std::string s) {
        if (s.size() >= 2
            && ((s.front() == '"'  && s.back() == '"')
             || (s.front() == '\'' && s.back() == '\''))) {
            return s.substr(1, s.size() - 2);
        }
        return s;
    };

    auto to_int = [](const std::string& v, int fallback) {
        try { return std::stoi(v); } catch (...) { return fallback; }
    };

    std::string line;
    while (std::getline(file, line)) {
        // Strip leading whitespace.
        size_t s = line.find_first_not_of(" \t");
        if (s == std::string::npos) continue;
        if (line[s] == '#') continue;
        line.erase(0, s);

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        trim(key);
        trim(val);
        val = unquote(val);

        if      (key == "shell")           c.m_shell           = val;
        else if (key == "theme")           c.m_themePath       = resolveThemePath(val);
        else if (key == "font_family")     c.m_fontFamily      = val;
        else if (key == "font_size")       c.m_fontSize        = to_int(val, c.m_fontSize);
        else if (key == "font_weight")     c.m_fontWeight      = val;
        else if (key == "use_bold") {
            std::string v = val;
            for (char& ch : v) ch = (char)tolower((unsigned char)ch);
            c.m_useBold = !(v == "no" || v == "off" || v == "false" || v == "0");
        }
        else if (key == "window_width")    c.m_windowWidth     = to_int(val, c.m_windowWidth);
        else if (key == "window_height")   c.m_windowHeight    = to_int(val, c.m_windowHeight);
        else if (key == "scrollback")      c.m_scrollback      = to_int(val, c.m_scrollback);
        else if (key == "opacity")         c.m_opacityPercent  = to_int(val, c.m_opacityPercent);
        else if (key == "cursor_style")    c.m_cursorStyle     = val;
        else if (key == "cursor_blink") {
            std::string v = val;
            for (char& ch : v) ch = (char)tolower((unsigned char)ch);
            c.m_cursorBlink = (v == "yes" || v == "on" || v == "true" || v == "1");
        }
        else if (key == "startup_command") c.m_startupCommand  = val;
        else if (key == "tabs")            c.m_tabsMode        = val;
        else if (key == "foreground")      c.m_foreground      = parseColor(val);
        else if (key == "background")      c.m_background      = parseColor(val);
        else if (key == "cursor_color")    c.m_cursorColor     = parseColor(val);
        else if (key == "selection_background") c.m_selectionBg = parseColor(val);
        else if (key == "selection_foreground") c.m_selectionFg = parseColor(val);
        else if (key == "padding_x")       c.m_paddingX        = to_int(val, c.m_paddingX);
        else if (key == "padding_y")       c.m_paddingY        = to_int(val, c.m_paddingY);
        else if (key.rfind("palette", 0) == 0 || key.rfind("color", 0) == 0) {
            // palette0..palette15 (or color0..color15) -> 16-colour ANSI palette
            std::string num = key.substr(key[0] == 'p' ? 7 : 5);
            int idx = to_int(num, -1);
            if (idx >= 0 && idx < 16) c.m_palette[idx] = parseColor(val);
        }
    }

    return c;
}
