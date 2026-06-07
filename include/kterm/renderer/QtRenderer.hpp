#pragma once
#include <QFont>
#include <QColor>
#include <string>
#include "kterm/renderer/Grid.hpp"

class QPainter;

namespace kterm::renderer {

class QtRenderer {
public:
    QtRenderer(const QFont& font, int cellWidth, int cellHeight);

    // Draw the entire terminal grid
    void render(QPainter& painter, const Grid& grid);

    // Resize the renderer when terminal size changes
    void resize(int cols, int rows);

    // Load theme colors from a JSON file
    void loadTheme(const std::string& path);

private:
    QFont m_font;
    int m_cellWidth;
    int m_cellHeight;

    int m_cols = 80;
    int m_rows = 24;

    // Theme colors
    QColor m_defaultFg = QColor(255, 255, 255);
    QColor m_defaultBg = QColor(0, 0, 0);

    // Draw a single cell
    void drawCell(QPainter& painter, int row, int col, const Cell& cell);
};

}