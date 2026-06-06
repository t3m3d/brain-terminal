#include "kterm/renderer/Grid.hpp"
#include <algorithm>

using namespace kterm::renderer;

Grid::Grid(int cols, int rows)
    : m_cols(cols), m_rows(rows),
      m_cursorRow(0), m_cursorCol(0),
      m_cells(rows, std::vector<Cell>(cols))
{
    // Default colors
    m_currentFG = 0xFFFFFFFF; // white
    m_currentBG = 0xFF000000; // black

    // Standard 16-color palette
    palette16[0]  = 0xFF000000; // black
    palette16[1]  = 0xFFAA0000; // red
    palette16[2]  = 0xFF00AA00; // green
    palette16[3]  = 0xFFAA5500; // yellow
    palette16[4]  = 0xFF0000AA; // blue
    palette16[5]  = 0xFFAA00AA; // magenta
    palette16[6]  = 0xFF00AAAA; // cyan
    palette16[7]  = 0xFFAAAAAA; // white
    palette16[8]  = 0xFF555555; // bright black
    palette16[9]  = 0xFFFF5555; // bright red
    palette16[10] = 0xFF55FF55; // bright green
    palette16[11] = 0xFFFFFF55; // bright yellow
    palette16[12] = 0xFF5555FF; // bright blue
    palette16[13] = 0xFFFF55FF; // bright magenta
    palette16[14] = 0xFF55FFFF; // bright cyan
    palette16[15] = 0xFFFFFFFF; // bright white

    // 256-color palette
    for (int i = 0; i < 256; i++) {
        if (i < 16) {
            palette256[i] = palette16[i];
        } else if (i >= 16 && i < 232) {
            int idx = i - 16;
            int r = (idx / 36) % 6;
            int g = (idx / 6) % 6;
            int b = idx % 6;
            r = r ? r * 40 + 55 : 0;
            g = g ? g * 40 + 55 : 0;
            b = b ? b * 40 + 55 : 0;
            palette256[i] = (0xFF << 24) | (r << 16) | (g << 8) | b;
        } else {
            int gray = (i - 232) * 10 + 8;
            palette256[i] = (0xFF << 24) | (gray << 16) | (gray << 8) | gray;
        }
    }
}

void Grid::resize(int cols, int rows) {
    m_cols = cols;
    m_rows = rows;
    m_cells.assign(rows, std::vector<Cell>(cols));
    m_cursorRow = 0;
    m_cursorCol = 0;
}

void Grid::clear() {
    for (auto& row : m_cells)
        for (auto& cell : row)
            cell = Cell();
}

void Grid::clearLine(int row) {
    if (row >= 0 && row < m_rows)
        m_cells[row].assign(m_cols, Cell());
}

// Erase from the cursor column to the end of the current row (CSI 0K). This
// is what shells emit (ESC[K) to clean the tail of a redrawn line — erasing
// the whole row instead wipes the prompt.
void Grid::eraseToLineEnd() {
    if (m_cursorRow >= 0 && m_cursorRow < m_rows)
        for (int c = m_cursorCol; c < m_cols; ++c)
            m_cells[m_cursorRow][c] = Cell();
}

// Erase from the cursor to the end of the screen (CSI 0J): rest of this row,
// then all rows below.
void Grid::eraseToScreenEnd() {
    eraseToLineEnd();
    for (int r = m_cursorRow + 1; r < m_rows; ++r)
        m_cells[r].assign(m_cols, Cell());
}

// Scroll the whole grid up one row: drop row 0, shift everything up, blank the
// new bottom row. Called when the cursor advances past the last row.
void Grid::scrollUp() {
    if (m_rows <= 0) return;
    m_absScroll++;                                    // one more line off the top
    m_history.push_back(m_cells[0]);                  // keep the scrolled-off row
    if (m_history.size() > 5000) m_history.pop_front();
    for (int r = 0; r < m_rows - 1; ++r)
        m_cells[r] = m_cells[r + 1];
    m_cells[m_rows - 1].assign(m_cols, Cell());
}

// Move to the next row, scrolling if we'd go past the bottom.
void Grid::lineFeed() {
    m_cursorRow++;
    if (m_cursorRow >= m_rows) {
        scrollUp();
        m_cursorRow = m_rows - 1;
    }
}

void Grid::putChar(char c) {
    putCodepoint(static_cast<unsigned char>(c));
}

void Grid::putCodepoint(uint32_t cp) {
    if (cp == '\n') {                // line feed -> next row, column 0
        m_cursorCol = 0;
        m_wrapPending = false;
        lineFeed();
        return;
    }
    if (cp == '\r') {                // carriage return -> column 0
        m_cursorCol = 0;
        m_wrapPending = false;
        return;
    }
    if (cp == '\b') {                // backspace -> move left (no erase)
        if (m_wrapPending) m_wrapPending = false;
        else if (m_cursorCol > 0) m_cursorCol--;
        return;
    }
    if (cp == '\t') {               // tab -> next 8-column stop
        m_wrapPending = false;
        m_cursorCol = ((m_cursorCol / 8) + 1) * 8;
        if (m_cursorCol >= m_cols) m_cursorCol = m_cols - 1;
        return;
    }
    if (cp < 0x20 || cp == 0x7f) {  // ignore other control chars (bell, etc.)
        return;
    }

    // Deferred ("phantom") wrap: a glyph in the last column parks the cursor
    // there; the actual wrap happens only when the NEXT glyph arrives. This is
    // standard VT behavior — without it, zsh's prompt cleanup leaves a stray %.
    if (m_wrapPending) {
        m_cursorCol = 0;
        lineFeed();
        m_wrapPending = false;
    }

    if (m_cursorRow < m_rows && m_cursorCol < m_cols) {
        Cell& cell = m_cells[m_cursorRow][m_cursorCol];
        cell.ch = cp;
        cell.fg = m_currentFG;
        cell.bg = m_currentBG;
        cell.attrs = m_currentAttrs;
    }

    if (m_cursorCol + 1 >= m_cols) m_wrapPending = true;  // park in last column
    else                          m_cursorCol++;
}

void Grid::cursorUp(int n) {
    m_cursorRow -= n;
    clampCursor();
}

void Grid::cursorDown(int n) {
    m_cursorRow += n;
    clampCursor();
}

void Grid::cursorForward(int n) {
    m_cursorCol += n;
    clampCursor();
}

void Grid::cursorBack(int n) {
    m_cursorCol -= n;
    clampCursor();
}

void Grid::setCursor(int row, int col) {
    m_cursorRow = row;
    m_cursorCol = col;
    clampCursor();
}

void Grid::setFG16(int index) {
    if (index >= 0 && index < 16)
        m_currentFG = palette16[index];
}

void Grid::setBG16(int index) {
    if (index >= 0 && index < 16)
        m_currentBG = palette16[index];
}

void Grid::setFG256(int index) {
    if (index >= 0 && index < 256)
        m_currentFG = palette256[index];
}

void Grid::setBG256(int index) {
    if (index >= 0 && index < 256)
        m_currentBG = palette256[index];
}

void Grid::setFGTrue(int r, int g, int b) {
    m_currentFG = (0xFF << 24) | (r << 16) | (g << 8) | b;
}

void Grid::setBGTrue(int r, int g, int b) {
    m_currentBG = (0xFF << 24) | (r << 16) | (g << 8) | b;
}

void Grid::setFGDefault() { m_currentFG = 0xFFFFFFFF; }  // sentinel -> renderer default fg
void Grid::setBGDefault() { m_currentBG = 0x00000000; }  // alpha 0 -> renderer default bg
void Grid::enableAttr(uint8_t flag)  { m_currentAttrs |= flag; }
void Grid::disableAttr(uint8_t flag) { m_currentAttrs &= ~flag; }

void Grid::resetAttributes() {
    m_currentFG = 0xFFFFFFFF;   // default fg sentinel
    m_currentBG = 0x00000000;   // default bg (transparent)
    m_currentAttrs = 0;
}

void Grid::clampCursor() {
    m_wrapPending = false;   // any explicit cursor move cancels a deferred wrap
    m_cursorRow = std::clamp(m_cursorRow, 0, m_rows - 1);
    m_cursorCol = std::clamp(m_cursorCol, 0, m_cols - 1);
}

const std::vector<std::vector<Cell>>& Grid::rows() const {
    return m_cells;
}