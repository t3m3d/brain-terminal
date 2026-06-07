#include "brain/input/InputHandler.hpp"

using namespace brain::input;

std::string InputHandler::translateToEscape(int key, Modifier) {
    switch (key) {
        case 16777235: return "\x1b[A"; // Up
        case 16777237: return "\x1b[B"; // Down
        case 16777236: return "\x1b[C"; // Right
        case 16777234: return "\x1b[D"; // Left
        case 16777220: return "\r";     // Enter
        case 16777219: return "\x7f";   // Backspace
        default:
            return std::string(1, char(key));
    }
}