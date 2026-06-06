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
    for (char c : text) {
        m_grid.putChar(c);
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