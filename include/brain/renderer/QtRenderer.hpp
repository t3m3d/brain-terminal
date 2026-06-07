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

    // Draw the terminal. scrollOffset>0 scrolls up into history (cursor hidden).
    // sel* give a selection range in VIEW coordinates (row 0 = top visible row),
    // normalized so (sr0,sc0) precedes (sr1,sc1) in reading order.
    void render(QPainter& painter, const Grid& grid, bool cursorVisible = true,
                int scrollOffset = 0, bool selActive = false,
                int sr0 = 0, int sc0 = 0, int sr1 = 0, int sc1 = 0);

    // Resize the renderer when terminal size changes
    void resize(int cols, int rows);

    // Load theme colors from a JSON file
    void loadTheme(const std::string& path);

    // Appearance, driven by config.
    void setDefaultFg(QColor c) { m_defaultFg = c; }
    void setDefaultBg(QColor c) { m_defaultBg = c; }
    void setCursorColor(QColor c) { m_cursorColor = c; m_cursorColorSet = true; }
    void setCursorStyle(const std::string& s) { m_cursorStyle = s; }
    void setPadding(int x, int y) { m_padX = x; m_padY = y; }
    void setSelectionColors(QColor bg, QColor fg) { m_selBg = bg; m_selFg = fg; }
    QColor defaultBg() const { return m_defaultBg; }
    QColor defaultFg() const { return m_defaultFg; }

private:
    QFont m_font;
    int m_cellWidth;
    int m_cellHeight;
    int m_ascent = 0;   // font baseline offset, for crisp text placement

    int m_cols = 80;
    int m_rows = 24;

    // Theme colors
    QColor m_defaultFg = QColor(0xCC, 0xCC, 0xCC);
    QColor m_defaultBg = QColor(0x11, 0x11, 0x11);
    QColor m_cursorColor = QColor(0xCC, 0xCC, 0xCC);
    bool   m_cursorColorSet = false;
    QColor m_selBg = QColor(0x44, 0x44, 0x66);
    QColor m_selFg = QColor(0xFF, 0xFF, 0xFF);
    std::string m_cursorStyle = "block";   // block | bar | underline
    int m_padX = 0, m_padY = 0;            // inner padding in px

    void drawCell(QPainter& painter, int row, int col, const Cell& cell, bool selected);
};

}
