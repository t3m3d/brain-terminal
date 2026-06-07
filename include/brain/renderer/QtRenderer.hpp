#pragma once
#include <QFont>
#include <QColor>
#include <string>
#include <array>
#include "brain/renderer/Grid.hpp"

class QPainter;

namespace brain::renderer {

class QtRenderer {
public:
    QtRenderer(const QFont& font, int cellWidth, int cellHeight);

    // Legacy entry point. Renders the live grid (no scrollback view, no
    // selection overlay). cursorVisible reflects DECSET ?25; the renderer
    // also gets it directly via renderWithView.
    void render(QPainter& painter, const Grid& grid, bool cursorVisible = true);

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

    // Appearance, driven by config.
    void setDefaultFg(QColor c)        { m_defaultFg = c; }
    void setDefaultBg(QColor c)        { m_defaultBg = c; }
    void setCursorColor(QColor c)      { m_cursorColor = c; m_cursorColorSet = true; }
    void setCursorStyle(const std::string& s) { m_cursorStyle = s; }
    void setPadding(int x, int y)      { m_padX = x; m_padY = y; }
    void setUseBold(bool b)            { m_useBold = b; }
    void setSelectionColors(QColor bg, QColor fg) { m_selBg = bg; m_selFg = fg; }
    QColor defaultFg() const           { return m_defaultFg; }
    QColor defaultBg() const           { return m_defaultBg; }

private:
    QFont m_font;
    int m_cellWidth;
    int m_cellHeight;
    int m_ascent = 0;

    int m_cols = 80;
    int m_rows = 24;

    QColor m_defaultFg   = QColor(0xCC, 0xCC, 0xCC);
    QColor m_defaultBg   = QColor(0x11, 0x11, 0x11);
    QColor m_cursorColor = QColor(0xCC, 0xCC, 0xCC);
    bool   m_cursorColorSet = false;
    QColor m_selBg       = QColor(0x44, 0x44, 0x66, 180);
    QColor m_selFg       = QColor(0xFF, 0xFF, 0xFF);
    std::string m_cursorStyle = "block";   // block | bar | underline
    int m_padX = 0, m_padY = 0;            // inner padding in px
    bool m_useBold = true;                 // honour SGR 1 (bold) attribute

    void drawCell(QPainter& painter, int row, int col, const Cell& cell, bool selected);
};

}
