#include "brain/theme/ThemeLoader.hpp"
#include "brain/theme/Theme.hpp"
#include <fstream>
#include <sstream>
#include <cctype>

using namespace brain::theme;

Theme ThemeLoader::loadFromFile(const std::string& path) {
    Theme theme;

    std::ifstream file(path);
    if (!file.is_open())
        return theme; // return default theme

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();

    auto findValue = [&](const std::string& key) -> std::string {

        size_t pos = json.find("\"" + key + "\"");
        if (pos == std::string::npos)
            return "";

        // Find colon
        pos = json.find(":", pos);
        if (pos == std::string::npos)
            return "";

        // Skip whitespace and colon
        while (pos < json.size() && (json[pos] == ':' || std::isspace(json[pos])))
            pos++;

        // Expect opening quote
        if (pos >= json.size() || json[pos] != '"')
            return "";

        size_t end = json.find("\"", pos + 1);
        if (end == std::string::npos)
            return "";

        return json.substr(pos + 1, end - pos - 1);
    };

    std::string fgHex = findValue("fg");
    std::string bgHex = findValue("bg");

    if (!fgHex.empty())
        theme.setFG(parseColor(fgHex));

    if (!bgHex.empty())
        theme.setBG(parseColor(bgHex));

    return theme;
}

uint32_t ThemeLoader::parseColor(const std::string& hex) {
    // Expect "#RRGGBB"
    if (hex.size() != 7 || hex[0] != '#')
        return 0xFFFFFFFF;

    unsigned int r = std::stoul(hex.substr(1, 2), nullptr, 16);
    unsigned int g = std::stoul(hex.substr(3, 2), nullptr, 16);
    unsigned int b = std::stoul(hex.substr(5, 2), nullptr, 16);

    return (0xFF << 24) | (r << 16) | (g << 8) | b;
}