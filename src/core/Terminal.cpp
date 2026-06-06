#include "kterm/core/Terminal.hpp"
#include <algorithm>

using namespace kterm::core;

Terminal::Terminal(int cols, int rows)
    : m_cols(cols),
      m_rows(rows),
      m_cursorRow(0),
      m_cursorCol(0),
      m_grid(cols, rows),
      m_parser(cols, rows) {}

void Terminal::resize(int cols, int rows) {
    m_cols = cols;
    m_rows = rows;
    m_grid.resize(cols, rows);
    m_parser.resize(cols, rows);
    m_cursorRow = 0;
    m_cursorCol = 0;
    if (m_renderCallback) m_renderCallback();
}

void Terminal::onPTYOutput(const std::vector<char>& data) {
    std::string s(data.begin(), data.end());

    m_parser.feed(
        s,
        [this](const std::string& text) {
            handleText(text);
        },
        [this](const parser::EscapeSequence& esc) {
            applyEscape(esc);
        }
    );

    if (m_renderCallback) m_renderCallback();
}

void Terminal::handleText(const std::string& text) {
    // Decode a UTF-8 byte stream into Unicode codepoints. Any incomplete
    // trailing sequence is carried over to the next call (m_utf8) so a glyph
    // split across two PTY reads still decodes correctly.
    std::string s = m_utf8 + text;
    m_utf8.clear();

    size_t i = 0;
    while (i < s.size()) {
        unsigned char b = static_cast<unsigned char>(s[i]);
        uint32_t cp;
        int len;
        if (b < 0x80)            { cp = b;          len = 1; }
        else if ((b >> 5) == 0x6){ cp = b & 0x1F;   len = 2; }
        else if ((b >> 4) == 0xE){ cp = b & 0x0F;   len = 3; }
        else if ((b >> 3) == 0x1E){ cp = b & 0x07;  len = 4; }
        else                     { cp = b;          len = 1; }  // invalid lead

        if (i + len > s.size()) {           // incomplete tail — save for next feed
            m_utf8 = s.substr(i);
            break;
        }
        for (int k = 1; k < len; ++k) {
            unsigned char cb = static_cast<unsigned char>(s[i + k]);
            if ((cb >> 6) != 0x2) { len = 1; cp = b; break; }  // bad continuation
            cp = (cp << 6) | (cb & 0x3F);
        }
        m_grid.putCodepoint(cp);
        i += len;
    }
}

void Terminal::applyEscape(const parser::EscapeSequence& seq) {
    using parser::EscapeType;

    switch (seq.type) {
        case EscapeType::CursorUp:
            m_grid.cursorUp(seq.value > 0 ? seq.value : 1);
            break;

        case EscapeType::CursorDown:
            m_grid.cursorDown(seq.value > 0 ? seq.value : 1);
            break;

        case EscapeType::CursorForward:
            m_grid.cursorForward(seq.value > 0 ? seq.value : 1);
            break;

        case EscapeType::CursorBack:
            m_grid.cursorBack(seq.value > 0 ? seq.value : 1);
            break;

        case EscapeType::SetCursorPos: {
            int row = std::max(0, seq.row - 1);
            int col = std::max(0, seq.col - 1);
            m_grid.setCursor(row, col);
            break;
        }

        case EscapeType::ClearScreen:
            m_grid.clear();
            m_grid.setCursor(0, 0);
            break;

        case EscapeType::ClearLine:
            // ESC[K = erase cursor -> end of line (using the grid's real
            // cursor, not Terminal's stale m_cursorRow).
            m_grid.eraseToLineEnd();
            break;

        case EscapeType::SetFGColor:
            m_grid.setFG16(seq.color);
            break;

        case EscapeType::SetBGColor:
            m_grid.setBG16(seq.color);
            break;

        case EscapeType::SetFGColor256:
            m_grid.setFG256(seq.color);
            break;

        case EscapeType::SetBGColor256:
            m_grid.setBG256(seq.color);
            break;

        case EscapeType::SetFGTrueColor:
            m_grid.setFGTrue(seq.r, seq.g, seq.b);
            break;

        case EscapeType::SetBGTrueColor:
            m_grid.setBGTrue(seq.r, seq.g, seq.b);
            break;

        case EscapeType::ResetAttributes:
            m_grid.resetAttributes();
            break;

        default:
            break;
    }
}