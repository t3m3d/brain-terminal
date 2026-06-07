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

    // Original ASCII fallback path (legacy callers).
    std::string translateToEscape(int keyCode, Modifier mod);

    // Preferred path: pass the QKeyEvent's text() too. For ordinary
    // printable keys we send `text` verbatim (so Shift/CapsLock land
    // the right case, and dead keys / IME compose correctly). Only
    // special / non-text keys (arrows, Enter, Esc, function keys,
    // Ctrl+letter combos, …) consult `keyCode`.
    std::string translate(int keyCode, Modifier mod, const std::string& text);
};

}
