#pragma once
#include <string>
#include <cstdint>
#include "Theme.hpp"

namespace brain::theme {

class ThemeLoader {
public:
    // Load a theme JSON file and return a Theme object
    static Theme loadFromFile(const std::string& path);

private:
    // Internal helpers for parsing JSON fields
    static uint32_t parseColor(const std::string& hex);
};

}