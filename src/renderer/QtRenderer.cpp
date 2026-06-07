#include "kterm/renderer/QtRenderer.hpp"
#include <QPainter>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

using namespace kterm::renderer;

QtRenderer::QtRenderer(const QFont& font, int cw, int ch)
    : m_font(font), m_cellWidth(cw), m_cellHeight(ch)
{
}

void QtRenderer::resize(int cols, int rows) {
    m_cols = cols;
    m_rows = rows;
}

void QtRenderer::loadTheme(const std::string& path) {
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly)) {
        return; // fallback to defaults
    }

    auto doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return;
    }

    auto obj = doc.object();

    if (obj.contains("foreground"))
        m_defaultFg = QColor(obj["foreground"].toString());

    if (obj.contains("background"))
        m_defaultBg = QColor(obj["background"].toString());
}

void QtRenderer::render(QPainter& painter, const Grid& grid) {
    painter.setFont(m_font);

    const auto& rows = grid.rows();
    for (int r = 0; r < (int)rows.size(); ++r) {
        for (int c = 0; c < (int)rows[r].size(); ++c) {
            drawCell(painter, r, c, rows[r][c]);
        }
    }
}

void QtRenderer::drawCell(QPainter& painter, int row, int col, const Cell& cell) {
    int x = col * m_cellWidth;
    int y = row * m_cellHeight;

    QColor bg(cell.bg);
    if (bg.alpha() == 0) bg = m_defaultBg;

    QColor fg(cell.fg);
    if (fg.alpha() == 0) fg = m_defaultFg;

    painter.fillRect(x, y, m_cellWidth, m_cellHeight, bg);
    painter.setPen(fg);

    if (cell.ch != 0) {
        char32_t cp = cell.ch;
        painter.drawText(x, y + m_cellHeight - 2, QString::fromUcs4(&cp, 1));
    }
}