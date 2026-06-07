#include "brain/Config.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>

using namespace brain;

// ------------------------------------------------------------
// Default config values
// ------------------------------------------------------------
Config Config::defaults() {
    return Config(
        "/bin/bash",
        "resources/themes/default.json",
        "Monospace",
        14
    );
}

// ------------------------------------------------------------
// find the correct config file to load
// ------------------------------------------------------------
static std::string findConfigPath(const std::string& overridePath) {
    // If user explicitly passed a path, use it
    if (!overridePath.empty()) {
        std::ifstream f(overridePath);
        if (f.good()) return overridePath;
    }

    // User config (~/.config/brain/brain.conf)
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

    // Built-in default config
    return "resources/config/brain.conf";
}

// ------------------------------------------------------------
// Load config from file (simple, human-friendly parser)
// ------------------------------------------------------------
Config Config::load(const std::string& path) {
    std::string finalPath = findConfigPath(path);
    std::ifstream file(finalPath);

    if (!file.is_open())
        return defaults();

    std::string shell = "/bin/bash";
    std::string theme = "resources/themes/default.json";
    std::string font  = "Monospace";
    int fontSize = 14;

    std::string line;
    while (std::getline(file, line)) {
        // Remove leading whitespace
        line.erase(0, line.find_first_not_of(" \t"));
        if (line.empty() || line[0] == '#')
            continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        // Trim spaces
        auto trim = [&](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t"));
            s.erase(s.find_last_not_of(" \t") + 1);
        };

        trim(key);
        trim(val);

        if (key == "shell") shell = val;
        else if (key == "theme") theme = val;
        else if (key == "font_family") font = val;
        else if (key == "font_size") fontSize = std::stoi(val);
    }

    return Config(shell, theme, font, fontSize);
}