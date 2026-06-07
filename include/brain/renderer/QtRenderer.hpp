#pragma once
#include <QFont>
#include <QColor>
#include <string>
#include "brain/renderer/Grid.hpp"

class QPainter;

namespace brain::renderer {

class QtRenderer {
public:
    QtRenderer(const QFont& font, int cellWidth, int cellHeight);

    // Legacy entry point — renders the live grid with no scrollback, no
    // selection, no cursor. Kept for tests; new callers use renderWithView.
    void render(QPainter& painter, const Grid& grid);

    // Full render: viewport offset (0 = live tail, N = N rows above tail),
    // optional selection rectangle in ABSOLUTE coordinates, and a cursor
    // hint. Pass nullptr for any of the four selection coords to skip the
    // selection overlay. focused chooses block vs outlined cursor.
    void renderWithView(
        QPainter& painter,
        const Grid& grid,
        int viewportOffset,
        const long long* selAnchorAbsRow,
        const int*       selAnchorCol,
        const long long* selFocusAbsRow,
        const int*       selFocusCol,
        bool focused,
        bool cursorVisible);

    void resize(int cols, int rows);
    void loadTheme(const std::string& path);

    // Cursor style: 0 = block, 1 = underline, 2 = bar (vertical i-beam).
    enum CursorStyle { CursorBlock = 0, CursorUnderline = 1, CursorBar = 2 };
    void setCursorStyle(CursorStyle s) { m_cursorStyle = s; }

    // Theme accessors (used to derive selection background, dim text, etc).
    QColor defaultFg() const { return m_defaultFg; }
    QColor defaultBg() const { return m_defaultBg; }

private:
    QFont m_font;
    int m_cellWidth;
    int m_cellHeight;
    int m_ascent = 0;

    int m_cols = 80;
    int m_rows = 24;

    QColor m_defaultFg = QColor(220, 220, 220);
    QColor m_defaultBg = QColor(0, 0, 0);
    QColor m_selectionBg = QColor(70, 90, 140, 180);   // semi-transparent blue
    QColor m_cursorColor = QColor(220, 220, 220);
    CursorStyle m_cursorStyle = CursorBlock;

    void drawCell(QPainter& painter, int row, int col, const Cell& cell, bool selected);
};

}
