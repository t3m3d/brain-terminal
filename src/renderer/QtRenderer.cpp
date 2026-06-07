#include "kterm/renderer/QtRenderer.hpp"
#include <QPainter>
#include <QFontMetrics>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

using namespace kterm::renderer;

QtRenderer::QtRenderer(const QFont& font, int cw, int ch)
    : m_font(font), m_cellWidth(cw), m_cellHeight(ch)
{
    m_ascent = QFontMetrics(font).ascent();
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

    // cell.fg/bg are 0xAARRGGBB; alpha 0 means "use the terminal default".
    // (QColor(QRgb) forces alpha to 255, so the old alpha()==0 check was dead;
    // read the high byte directly and use fromRgba for real colours.)
    QColor bg = ((cell.bg >> 24) == 0) ? m_defaultBg : QColor::fromRgba(cell.bg);
    QColor fg = ((cell.fg >> 24) == 0) ? m_defaultFg : QColor::fromRgba(cell.fg);

    painter.fillRect(x, y, m_cellWidth, m_cellHeight, bg);
    painter.setPen(fg);

    if (cell.ch != 0) {
        char32_t cp = cell.ch;
        painter.drawText(x, y + m_ascent, QString::fromUcs4(&cp, 1));
    }
}