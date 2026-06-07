#include "brain/input/InputHandler.hpp"

using namespace brain::input;

// Qt::Key constants we care about. Listed inline rather than pulling QtCore
// into the header — InputHandler is the only place that needs them and
// stays Qt-free that way.
enum : int {
    K_Backspace = 0x01000003,
    K_Tab       = 0x01000001,
    K_Backtab   = 0x01000002,
    K_Return    = 0x01000004,
    K_Enter     = 0x01000005,
    K_Escape    = 0x01000000,
    K_Delete    = 0x01000007,
    K_Home      = 0x01000010,
    K_End       = 0x01000011,
    K_Left      = 0x01000012,
    K_Up        = 0x01000013,
    K_Right     = 0x01000014,
    K_Down      = 0x01000015,
    K_PageUp    = 0x01000016,
    K_PageDown  = 0x01000017,
    K_Insert    = 0x01000006,
    K_F1        = 0x01000030,
    K_F2        = 0x01000031,
    K_F3        = 0x01000032,
    K_F4        = 0x01000033,
    K_F5        = 0x01000034,
    K_F6        = 0x01000035,
    K_F7        = 0x01000036,
    K_F8        = 0x01000037,
    K_F9        = 0x01000038,
    K_F10       = 0x01000039,
    K_F11       = 0x0100003a,
    K_F12       = 0x0100003b,
};

static std::string specialKeySeq(int key, Modifier mod) {
    const bool shift = (mod == Modifier::Shift);
    switch (key) {
        case K_Up:        return "\x1b[A";
        case K_Down:      return "\x1b[B";
        case K_Right:     return "\x1b[C";
        case K_Left:      return "\x1b[D";
        case K_Home:      return "\x1b[H";
        case K_End:       return "\x1b[F";
        case K_PageUp:    return "\x1b[5~";
        case K_PageDown:  return "\x1b[6~";
        case K_Insert:    return "\x1b[2~";
        case K_Delete:    return "\x1b[3~";
        case K_Return:
        case K_Enter:     return "\r";
        case K_Backspace: return "\x7f";
        case K_Tab:       return shift ? "\x1b[Z" : "\t";
        case K_Backtab:   return "\x1b[Z";
        case K_Escape:    return "\x1b";
        case K_F1:  return "\x1bOP";
        case K_F2:  return "\x1bOQ";
        case K_F3:  return "\x1bOR";
        case K_F4:  return "\x1bOS";
        case K_F5:  return "\x1b[15~";
        case K_F6:  return "\x1b[17~";
        case K_F7:  return "\x1b[18~";
        case K_F8:  return "\x1b[19~";
        case K_F9:  return "\x1b[20~";
        case K_F10: return "\x1b[21~";
        case K_F11: return "\x1b[23~";
        case K_F12: return "\x1b[24~";
        default:    return {};
    }
}

std::string InputHandler::translateToEscape(int key, Modifier mod) {
    std::string s = specialKeySeq(key, mod);
    if (!s.empty()) return s;
    // Legacy fallback — produces the wrong case for letters because Qt
    // letter constants are uppercase. translate() below is the right path.
    return std::string(1, char(key));
}

std::string InputHandler::translate(int key, Modifier mod, const std::string& text) {
    // Special keys (arrows, F-keys, …) override any text the event carried.
    std::string s = specialKeySeq(key, mod);
    if (!s.empty()) return s;

    // Ctrl + letter / symbol → C0 control. Qt::Key constants for letters
    // are the uppercase ASCII byte, so the math is trivial.
    if (mod == Modifier::Ctrl) {
        if (key >= 'A' && key <= 'Z') return std::string(1, char(key - 'A' + 1));
        if (key >= 'a' && key <= 'z') return std::string(1, char(key - 'a' + 1));
        switch (key) {
            case '@': return std::string(1, '\x00');
            case '[': return std::string(1, '\x1b');
            case '\\': return std::string(1, '\x1c');
            case ']': return std::string(1, '\x1d');
            case '^': return std::string(1, '\x1e');
            case '_': return std::string(1, '\x1f');
            case ' ': return std::string(1, '\x00');
        }
    }

    // Alt + char → ESC <char>. xterm convention.
    if (mod == Modifier::Alt && !text.empty()) {
        std::string out = "\x1b";
        out += text;
        return out;
    }

    // Plain printable: trust the event text — it already accounts for
    // Shift, CapsLock, dead keys, IME, and non-ASCII layouts. This is the
    // line that fixes "shows uppercase, runs lowercase".
    if (!text.empty()) return text;

    // No text and no special mapping → drop the event.
    return {};
}
