#include "brain/Config.hpp"
#include <fstream>
#include <sstream>

using namespace brain;

namespace {

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
        else if (key == "theme")           c.m_themePath       = val;
        else if (key == "font_family")     c.m_fontFamily      = val;
        else if (key == "font_size")       c.m_fontSize        = to_int(val, c.m_fontSize);
        else if (key == "window_width")    c.m_windowWidth     = to_int(val, c.m_windowWidth);
        else if (key == "window_height")   c.m_windowHeight    = to_int(val, c.m_windowHeight);
        else if (key == "scrollback")      c.m_scrollback      = to_int(val, c.m_scrollback);
        else if (key == "opacity")         c.m_opacityPercent  = to_int(val, c.m_opacityPercent);
        else if (key == "cursor_style")    c.m_cursorStyle     = val;
        else if (key == "startup_command") c.m_startupCommand  = val;
    }

    return c;
}
