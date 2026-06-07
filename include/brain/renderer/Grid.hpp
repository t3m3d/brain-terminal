#pragma once
#include <vector>
#include <deque>
#include "Cell.hpp"

namespace brain::renderer {

class Grid {
public:
    Grid(int cols, int rows);

    void resize(int cols, int rows);
    void clear();
    void clearLine(int row);
    void eraseToLineEnd();     // erase from cursor column to end of current row
    void eraseToScreenEnd();   // erase from cursor to end of screen
    void eraseToLineStart();   // erase from start of line to cursor
    void eraseToScreenStart(); // erase from top of screen to cursor

    // CSI L / CSI M / CSI @ / CSI P / CSI X — vim/less repaint primitives.
    void insertLines(int count);   // at cursor row, push following rows down
    void deleteLines(int count);   // at cursor row, pull following rows up
    void insertChars(int count);   // at cursor pos, shift right
    void deleteChars(int count);   // at cursor pos, shift left
    void eraseChars(int count);    // overwrite N cells with blanks, no shift

    // Whole-grid snapshot. Used for the alternate screen buffer (snapshot
    // before vim enters; restore on exit). NOT scrollback — history is
    // intentionally NOT touched here.
    struct Snapshot {
        std::vector<std::vector<Cell>> cells;
        int cursorRow = 0;
        int cursorCol = 0;
        uint8_t  currentAttrs = 0;
        uint32_t currentFG    = 0;
        uint32_t currentBG    = 0;
    };
    Snapshot snapshot() const;
    void restore(const Snapshot& s);

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

    // OSC 8 link id stamped onto every new cell until cleared (0).
    void setCurrentLink(uint16_t id) { m_currentLink = id; }
    uint16_t currentLink() const { return m_currentLink; }
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
    void setHistoryMax(int n) {
        m_historyMax = (n < 0) ? 0 : n;
        while ((int)m_history.size() > m_historyMax) m_history.pop_front();
    }

    // Total lines ever scrolled off the top (for stable absolute line numbers).
    long absScroll() const { return m_absScroll; }

    // Bumped on any change to visible cell CONTENT (not cursor moves). Lets the
    // GPU renderer cache grid geometry and rebuild only when this changes.
    uint64_t generation() const { return m_generation; }
private:
    int m_cols;
    int m_rows;
    int m_cursorRow;
    int m_cursorCol;

    // Current drawing attributes
    uint32_t m_currentFG;
    uint32_t m_currentBG;
    uint8_t  m_currentAttrs = 0;   // CellAttr flags applied to new cells
    uint16_t m_currentLink  = 0;   // OSC 8 hyperlink id stamped on new cells
    bool     m_wrapPending  = false; // deferred wrap: cursor parked in last column

    // Palettes
    uint32_t palette16[16];
    uint32_t palette256[256];

    std::vector<std::vector<Cell>> m_cells;
    std::deque<std::vector<Cell>> m_history;   // scrolled-off rows (oldest..newest)
    int  m_historyMax = 5000;                  // configurable cap, see setHistoryMax
    long m_absScroll = 0;                      // count of lines scrolled off the top, ever
    uint64_t m_generation = 0;                 // bumped on visible content change

    void clampCursor();
    void lineFeed();   // advance one row, scrolling at the bottom
};

}