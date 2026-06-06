#pragma once
#include <string>
#include <functional>
#include <vector>

namespace kterm::parser {

enum class EscapeType {
    Text,

    // Cursor movement
    CursorUp,
    CursorDown,
    CursorForward,
    CursorBack,
    SetCursorPos,

    // Screen operations
    ClearScreen,
    ClearLine,

    // Basic 16-color SGR
    SetFGColor,
    SetBGColor,

    // Extended color modes
    SetFGColor256,
    SetBGColor256,
    SetFGTrueColor,
    SetBGTrueColor,

    // Reset
    ResetAttributes,

    // Full SGR (colors + text attributes); all params in .params
    SGR,

    // DEC/ANSI mode set/reset (ESC[...h / ESC[...l); .value = mode,
    // .privateMode = true for ESC[?...
    SetMode,
    ResetMode,

    Unknown
};

struct EscapeSequence {
    EscapeType type = EscapeType::Unknown;

    // Generic numeric value (cursor movement, etc.)
    int value = 0;

    // Cursor positioning
    int row = 0;
    int col = 0;

    // Color index (0–255)
    int color = 0;

    // Truecolor RGB
    int r = 0;
    int g = 0;
    int b = 0;

    // All SGR parameters (for EscapeType::SGR), applied left-to-right.
    std::vector<int> params;

    // True for ESC[? ... (DEC private mode), used with Set/ResetMode.
    bool privateMode = false;
};

class AnsiParser {
public:
    AnsiParser(int cols, int rows);

    void resize(int cols, int rows);

    void feed(
        const std::string& input,
        std::function<void(const std::string&)> onText,
        std::function<void(const EscapeSequence&)> onEscape
    );
private:
    std::string m_buffer;

    EscapeSequence parseCSI(const std::string& seq);
    bool isCSI(const std::string& seq) const;
};

}