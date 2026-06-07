#pragma once
#include <string>
#include <functional>
#include <vector>

namespace brain::parser {

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

    // OSC string (ESC ] ... BEL/ST); payload in .osc (e.g. "133;A", "0;title").
    OSC,

    // Single-byte ESC sequences (ESC 7 / ESC 8).
    SaveCursor,        // DECSC: ESC 7  AND  CSI s
    RestoreCursor,     // DECRC: ESC 8  AND  CSI u

    // CSI r: set top/bottom scroll region. .row = top, .col = bottom (1-based).
    SetScrollRegion,

    // CSI J 1: erase from start to cursor.
    EraseInDisplayStart,
    // CSI K 1: erase from start of line to cursor.
    EraseInLineStart,

    // CSI L / CSI M: insert/delete lines at cursor (vim, less). .value = count.
    InsertLines,
    DeleteLines,
    // CSI @ / CSI P / CSI X: insert blank / delete / erase chars in line.
    InsertChars,
    DeleteChars,
    EraseChars,

    Unknown
};

struct EscapeSequence {
    EscapeType type = EscapeType::Unknown;

    // Generic numeric value (cursor movement, etc.)
    int value = 0;

    // Cursor positioning
    int row = 0;
    int col = 0;

    // Color index (0-255)
    int color = 0;

    // Truecolor RGB
    int r = 0;
    int g = 0;
    int b = 0;

    // All SGR parameters (for EscapeType::SGR), applied left-to-right.
    std::vector<int> params;

    // True for ESC[? ... (DEC private mode), used with Set/ResetMode.
    bool privateMode = false;

    // OSC payload (for EscapeType::OSC).
    std::string osc;
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