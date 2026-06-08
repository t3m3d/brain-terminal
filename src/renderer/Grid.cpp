#include "brain/renderer/Grid.hpp"
#include "brain/renderer/CharWidth.hpp"
#include <algorithm>

using namespace brain::renderer;

Grid::Grid(int cols, int rows)
    : m_cols(cols), m_rows(rows),
      m_cursorRow(0), m_cursorCol(0),
      m_cells(rows, std::vector<Cell>(cols)),
      m_wrapped(rows, 0)
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
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;

    // A width change rewraps content (reflow); a height-only change just keeps
    // the bottom of the screen where the prompt and cursor live.
    if (cols != m_cols) { reflow(cols, rows); return; }

    std::vector<std::vector<Cell>> next(rows, std::vector<Cell>(cols));
    std::vector<uint8_t> nextWrap(rows, 0);
    int copyRows = std::min(rows, m_rows);
    int srcStart = (m_rows > rows) ? (m_rows - rows) : 0;
    for (int r = 0; r < copyRows; ++r) {
        next[r] = m_cells[srcStart + r];
        nextWrap[r] = m_wrapped[srcStart + r];
    }
    m_cells = std::move(next);
    m_wrapped = std::move(nextWrap);
    m_rows = rows;
    m_scrollTop = 0;
    m_scrollBottom = -1;

    m_cursorRow -= srcStart;
    if (m_cursorRow < 0) m_cursorRow = 0;
    if (m_cursorRow >= rows) m_cursorRow = rows - 1;
    if (m_cursorCol >= cols) m_cursorCol = cols - 1;
    m_wrapPending = false;
    m_generation++;
}

// Rewrap all content (history + visible) to a new width. Logical lines are
// reconstructed by joining soft-wrapped rows, then split to the new width.
void Grid::reflow(int newCols, int newRows) {
    // 1. Reconstruct logical lines, tracking the cursor's logical line+offset.
    std::vector<std::vector<Cell>> logical;
    std::vector<Cell> acc;
    long curLine = -1, curOff = 0;
    bool started = false;

    auto consume = [&](const std::vector<Cell>& cells, bool wrapped, bool isCursor, int cursorCol) {
        int len = (int)cells.size();
        if (!wrapped) { while (len > 0 && (cells[len-1].ch == ' ' || cells[len-1].ch == 0)) --len; }
        if (isCursor) {
            int off = 0;
            for (int c = 0; c < cursorCol && c < (int)cells.size(); ++c)
                if (cells[c].ch != 0) ++off;
            curLine = (long)logical.size();
            curOff  = (long)acc.size() + off;
        }
        for (int c = 0; c < len; ++c)
            if (cells[c].ch != 0) acc.push_back(cells[c]);   // drop wide-spacers
        started = true;
        if (!wrapped) { logical.push_back(std::move(acc)); acc.clear(); started = false; }
    };

    for (size_t i = 0; i < m_history.size(); ++i)
        consume(m_history[i], m_histWrapped[i] != 0, false, 0);
    for (int r = 0; r < m_rows; ++r)
        consume(m_cells[r], m_wrapped[r] != 0, r == m_cursorRow, m_cursorCol);
    if (started) logical.push_back(std::move(acc));

    // Drop trailing blank lines below the cursor — they're just empty screen
    // space, not content, and shouldn't be rewrapped into the scrollback.
    long lastNonEmpty = -1;
    for (long i = 0; i < (long)logical.size(); ++i)
        if (!logical[i].empty()) lastNonEmpty = i;
    long keep = std::max(lastNonEmpty, curLine);
    if (keep < 0) keep = 0;
    if ((long)logical.size() > keep + 1) logical.resize((size_t)(keep + 1));

    // 2. Re-wrap each logical line to newCols, tracking the new cursor cell.
    std::vector<std::vector<Cell>> out;
    std::vector<uint8_t> outWrap;
    long newCurAbs = -1; int newCurCol = 0;

    for (size_t li = 0; li < logical.size(); ++li) {
        const std::vector<Cell>& line = logical[li];
        int n = (int)line.size(), c = 0;
        do {
            std::vector<Cell> row;
            int produced = 0;
            while (c < n) {
                int w = charWidth(line[c].ch); if (w < 1) w = 1;
                if (produced + w > newCols) break;
                if ((long)li == curLine && c == curOff && newCurAbs < 0) {
                    newCurAbs = (long)out.size(); newCurCol = produced;
                }
                row.push_back(line[c]);
                if (w == 2) { Cell sp = line[c]; sp.ch = 0; row.push_back(sp); }
                produced += w;
                ++c;
            }
            if ((long)li == curLine && curOff >= n && c >= n && newCurAbs < 0) {
                newCurAbs = (long)out.size(); newCurCol = produced;
            }
            while ((int)row.size() < newCols) row.push_back(Cell());
            bool more = (c < n);
            out.push_back(std::move(row));
            outWrap.push_back(more ? 1 : 0);
        } while (c < n);
    }
    if (out.empty()) { out.emplace_back(newCols, Cell()); outWrap.push_back(0); }

    // 3. Split into history (older) + the last newRows visible rows.
    int total = (int)out.size();
    int visStart = std::max(0, total - newRows);
    m_history.clear(); m_histWrapped.clear();
    for (int i = 0; i < visStart; ++i) {
        m_history.push_back(std::move(out[i]));
        m_histWrapped.push_back(outWrap[i]);
    }
    while ((int)m_history.size() > m_historyMax) { m_history.pop_front(); m_histWrapped.pop_front(); }

    m_cells.assign(newRows, std::vector<Cell>(newCols));
    m_wrapped.assign(newRows, 0);
    int vis = total - visStart;
    for (int r = 0; r < vis; ++r) {
        m_cells[r] = std::move(out[visStart + r]);
        m_wrapped[r] = outWrap[visStart + r];
    }

    // 4. Place the cursor, reset scroll region, re-baseline absScroll.
    if (newCurAbs < 0) newCurAbs = total - 1;
    m_cursorRow = (int)(newCurAbs - visStart);
    if (m_cursorRow < 0) m_cursorRow = 0;
    if (m_cursorRow >= newRows) m_cursorRow = newRows - 1;
    m_cursorCol = std::min(newCurCol, newCols - 1);

    m_cols = newCols;
    m_rows = newRows;
    m_scrollTop = 0;
    m_scrollBottom = -1;
    m_wrapPending = false;
    m_absScroll = (long)m_history.size();
    m_generation++;
}

void Grid::clear() {
    for (auto& row : m_cells)
        for (auto& cell : row)
            cell = Cell();
    std::fill(m_wrapped.begin(), m_wrapped.end(), (uint8_t)0);
    m_generation++;
}

void Grid::clearLine(int row) {
    if (row >= 0 && row < m_rows) {
        m_cells[row].assign(m_cols, Cell());
        m_wrapped[row] = 0;
    }
    m_generation++;
}

// Erase from the cursor column to end of row (CSI 0K). Shells emit ESC[K to
// clean the tail of a redrawn line; erasing the whole row would wipe the prompt.
void Grid::eraseToLineEnd() {
    if (m_cursorRow >= 0 && m_cursorRow < m_rows) {
        for (int c = m_cursorCol; c < m_cols; ++c)
            m_cells[m_cursorRow][c] = Cell();
        m_wrapped[m_cursorRow] = 0;   // content now ends on this row
    }
    m_generation++;
}

// Erase from the cursor to the end of the screen (CSI 0J): rest of this row,
// then all rows below.
void Grid::eraseToScreenEnd() {
    eraseToLineEnd();
    for (int r = m_cursorRow + 1; r < m_rows; ++r) {
        m_cells[r].assign(m_cols, Cell());
        m_wrapped[r] = 0;
    }
    m_generation++;
}

// Scroll the WHOLE grid up one row: drop row 0 to history, shift, blank
// the bottom row. Called when no explicit scroll region is active and
// the cursor advances past the last row.
void Grid::scrollUp() {
    if (m_rows <= 0) return;
    m_generation++;
    m_absScroll++;                                    // one more line off the top
    if (m_historyMax > 0) {
        m_history.push_back(m_cells[0]);              // keep the scrolled-off row
        m_histWrapped.push_back(m_wrapped.empty() ? 0 : m_wrapped[0]);
        if ((int)m_history.size() > m_historyMax) { m_history.pop_front(); m_histWrapped.pop_front(); }
    }
    for (int r = 0; r < m_rows - 1; ++r) {
        m_cells[r] = m_cells[r + 1];
        m_wrapped[r] = m_wrapped[r + 1];
    }
    m_cells[m_rows - 1].assign(m_cols, Cell());
    m_wrapped[m_rows - 1] = 0;
}

// Move to the next row. If the cursor reaches the BOTTOM of the active
// scroll region, scroll only within the region — top row of region
// rolls off (NOT into history; xterm behaviour) and the bottom row of
// the region becomes blank. Rows outside the region don't move.
void Grid::lineFeed() {
    int top = m_scrollTop;
    int bot = (m_scrollBottom < 0) ? m_rows - 1
                                   : std::min(m_scrollBottom, m_rows - 1);
    if (top < 0) top = 0;
    if (bot < top) bot = m_rows - 1;

    bool fullScreen = (top == 0 && bot == m_rows - 1);

    m_cursorRow++;
    if (m_cursorRow > bot) {
        if (fullScreen) {
            scrollUp();
        } else {
            m_generation++;
            for (int r = top; r < bot; ++r) { m_cells[r] = m_cells[r + 1]; m_wrapped[r] = m_wrapped[r + 1]; }
            m_cells[bot].assign(m_cols, Cell());
            m_wrapped[bot] = 0;
        }
        m_cursorRow = bot;
    }
}

void Grid::putChar(char c) {
    putCodepoint(static_cast<unsigned char>(c));
}

void Grid::putCodepoint(uint32_t cp) {
    if (cp == '\n') {                // line feed -> next row, column 0
        m_cursorCol = 0;
        m_wrapPending = false;
        if (m_cursorRow < (int)m_wrapped.size()) m_wrapped[m_cursorRow] = 0;  // hard line end
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
    // there, and the wrap only happens when the next glyph arrives. Standard VT
    // behavior; without it zsh's prompt cleanup leaves a stray %.
    if (m_wrapPending) {
        if (m_cursorRow < (int)m_wrapped.size()) m_wrapped[m_cursorRow] = 1;  // soft wrap (continues)
        m_cursorCol = 0;
        lineFeed();
        m_wrapPending = false;
    }

    int w = charWidth(cp);
    if (w == 0) return;   // combining mark / zero-width: drop, keep alignment

    // A double-width glyph can't straddle the right edge — wrap to the next
    // line first, leaving the final column blank (standard VT behaviour).
    if (w == 2 && m_cursorCol == m_cols - 1) {
        m_cursorCol = 0;
        lineFeed();
    }

    if (m_cursorRow < m_rows && m_cursorCol < m_cols) {
        Cell& cell = m_cells[m_cursorRow][m_cursorCol];
        cell.ch = cp;
        cell.fg = m_currentFG;
        cell.bg = m_currentBG;
        cell.attrs = m_currentAttrs;
        cell.ulStyle = m_currentUlStyle;
        cell.ulColor = m_currentUlColor;
        cell.link = m_currentLink;
        // Wide glyph: park a continuation cell (ch = 0) in the next column. The
        // renderer draws nothing for it and the wide glyph from this cell
        // overflows into it; copying skips ch == 0 so no stray NUL is emitted.
        if (w == 2 && m_cursorCol + 1 < m_cols) {
            Cell& sp = m_cells[m_cursorRow][m_cursorCol + 1];
            sp.ch = 0;
            sp.fg = m_currentFG;
            sp.bg = m_currentBG;
            sp.attrs = m_currentAttrs;
            sp.link = m_currentLink;
        }
        m_generation++;
    }

    if (m_cursorCol + w >= m_cols) m_wrapPending = true;   // park at right edge
    else                          m_cursorCol += w;
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

void Grid::setUnderlineColor256(int idx) {
    if (idx >= 0 && idx < 256)
        m_currentUlColor = palette256[idx];
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
    m_currentUlStyle = 0;
    m_currentUlColor = 0;
}

void Grid::clampCursor() {
    m_wrapPending = false;   // any explicit cursor move cancels a deferred wrap
    m_cursorRow = std::clamp(m_cursorRow, 0, m_rows - 1);
    m_cursorCol = std::clamp(m_cursorCol, 0, m_cols - 1);
}

const std::vector<std::vector<Cell>>& Grid::rows() const {
    return m_cells;
}

// CSI 1J — erase from start of screen to cursor (inclusive).
void Grid::eraseToScreenStart() {
    for (int r = 0; r < m_cursorRow && r < m_rows; ++r)
        m_cells[r].assign(m_cols, Cell());
    if (m_cursorRow >= 0 && m_cursorRow < m_rows) {
        for (int c = 0; c <= m_cursorCol && c < m_cols; ++c)
            m_cells[m_cursorRow][c] = Cell();
    }
    m_generation++;
}

// CSI 1K — erase from start of line to cursor (inclusive).
void Grid::eraseToLineStart() {
    if (m_cursorRow < 0 || m_cursorRow >= m_rows) return;
    for (int c = 0; c <= m_cursorCol && c < m_cols; ++c)
        m_cells[m_cursorRow][c] = Cell();
    m_generation++;
}

// CSI L (n) — insert n blank rows AT the cursor row. Rows at cursor and
// below shift down; the bottom n rows fall off (they do NOT enter the
// scrollback history — historic behaviour for IL).
void Grid::insertLines(int count) {
    if (count <= 0 || m_cursorRow < 0 || m_cursorRow >= m_rows) return;
    int n = std::min(count, m_rows - m_cursorRow);
    for (int r = m_rows - 1; r >= m_cursorRow + n; --r)
        m_cells[r] = m_cells[r - n];
    for (int r = m_cursorRow; r < m_cursorRow + n; ++r)
        m_cells[r].assign(m_cols, Cell());
    m_generation++;
}

// CSI M (n) — delete n rows starting at the cursor row. Following rows
// pull up; the bottom n rows become blank. Counterpart of IL above.
void Grid::deleteLines(int count) {
    if (count <= 0 || m_cursorRow < 0 || m_cursorRow >= m_rows) return;
    int n = std::min(count, m_rows - m_cursorRow);
    for (int r = m_cursorRow; r < m_rows - n; ++r)
        m_cells[r] = m_cells[r + n];
    for (int r = m_rows - n; r < m_rows; ++r)
        m_cells[r].assign(m_cols, Cell());
    m_generation++;
}

void Grid::insertChars(int count) {
    if (count <= 0 || m_cursorRow < 0 || m_cursorRow >= m_rows) return;
    auto& row = m_cells[m_cursorRow];
    int n = std::min(count, m_cols - m_cursorCol);
    for (int c = m_cols - 1; c >= m_cursorCol + n; --c) row[c] = row[c - n];
    for (int c = m_cursorCol; c < m_cursorCol + n; ++c) row[c] = Cell();
    m_generation++;
}

void Grid::deleteChars(int count) {
    if (count <= 0 || m_cursorRow < 0 || m_cursorRow >= m_rows) return;
    auto& row = m_cells[m_cursorRow];
    int n = std::min(count, m_cols - m_cursorCol);
    for (int c = m_cursorCol; c < m_cols - n; ++c) row[c] = row[c + n];
    for (int c = m_cols - n; c < m_cols; ++c) row[c] = Cell();
    m_generation++;
}

void Grid::eraseChars(int count) {
    if (count <= 0 || m_cursorRow < 0 || m_cursorRow >= m_rows) return;
    auto& row = m_cells[m_cursorRow];
    int end = std::min(m_cursorCol + count, m_cols);
    for (int c = m_cursorCol; c < end; ++c) row[c] = Cell();
    m_generation++;
}

Grid::Snapshot Grid::snapshot() const {
    Snapshot s;
    s.cells = m_cells;
    s.cursorRow = m_cursorRow;
    s.cursorCol = m_cursorCol;
    s.currentAttrs = m_currentAttrs;
    s.currentFG = m_currentFG;
    s.currentBG = m_currentBG;
    return s;
}

void Grid::restore(const Snapshot& s) {
    // Restore is for the altscreen exit path. We trust the snapshot to
    // have been taken at the current size; if the user resized while in
    // altscreen, fall back to clearing rather than crashing.
    if ((int)s.cells.size() != m_rows ||
        (s.cells.empty() ? 0 : (int)s.cells[0].size()) != m_cols) {
        clear();
        return;
    }
    m_cells = s.cells;
    m_cursorRow = s.cursorRow;
    m_cursorCol = s.cursorCol;
    m_currentAttrs = s.currentAttrs;
    m_currentFG    = s.currentFG;
    m_currentBG    = s.currentBG;
    m_generation++;
}