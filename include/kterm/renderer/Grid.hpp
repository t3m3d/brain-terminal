#pragma once
#include <vector>
#include <deque>
#include "Cell.hpp"

namespace kterm::renderer {

class Grid {
public:
    Grid(int cols, int rows);

    void resize(int cols, int rows);
    void clear();
    void clearLine(int row);
    void eraseToLineEnd();   // erase from cursor column to end of current row
    void eraseToScreenEnd(); // erase from cursor to end of screen

    void putChar(char c);
    void putCodepoint(uint32_t cp);   // place a Unicode codepoint at the cursor
    void scrollUp();                  // scroll the whole grid up one row

    // Cursor movement
    void cursorUp(int n);
    void cursorDown(int n);
    void cursorForward(int n);
    void cursorBack(int n);
    void setCursor(int row, int col);

    // Color + attribute control
    void setFG16(int index);
    void setBG16(int index);
    void setFG256(int index);
    void setBG256(int index);
    void setFGTrue(int r, int g, int b);
    void setBGTrue(int r, int g, int b);
    void setFGDefault();
    void setBGDefault();
    void enableAttr(uint8_t flag);   // CellAttr bit
    void disableAttr(uint8_t flag);
    void resetAttributes();

    const std::vector<std::vector<Cell>>& rows() const;

    // Cursor position (for the renderer to draw the caret).
    int cursorRow() const { return m_cursorRow; }
    int cursorCol() const { return m_cursorCol; }
    int cols() const { return m_cols; }
    int rowCount() const { return m_rows; }

    // Scrollback history (rows that scrolled off the top).
    int historyLines() const { return (int)m_history.size(); }
    const std::vector<Cell>& historyRow(int i) const { return m_history[i]; }
private:
    int m_cols;
    int m_rows;
    int m_cursorRow;
    int m_cursorCol;

    // Current drawing attributes
    uint32_t m_currentFG;
    uint32_t m_currentBG;
    uint8_t  m_currentAttrs = 0;   // CellAttr flags applied to new cells

    // Palettes
    uint32_t palette16[16];
    uint32_t palette256[256];

    std::vector<std::vector<Cell>> m_cells;
    std::deque<std::vector<Cell>> m_history;   // scrolled-off rows (oldest..newest)

    void clampCursor();
    void lineFeed();   // advance one row, scrolling at the bottom
};

}