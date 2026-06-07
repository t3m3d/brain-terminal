#pragma once
#include <string>

namespace brain::input {

enum class Modifier {
    None,
    Shift,
    Ctrl,
    Alt
};

class InputHandler {
public:
    void handleKey(int keyCode, Modifier mod);
    void handleMouse(int x, int y, bool leftClick);

    std::string translateToEscape(int keyCode, Modifier mod);
};

}