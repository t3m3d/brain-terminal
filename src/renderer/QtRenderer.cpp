#include "brain/renderer/QtRenderer.hpp"
#include <QPainter>
#include <QFontMetrics>
#include <utility>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

using namespace brain::renderer;

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

void QtRenderer::render(QPainter& painter, const Grid& grid, bool cursorVisible) {
    painter.setFont(m_font);

    const auto& rows = grid.rows();
    for (int r = 0; r < (int)rows.size(); ++r)
        for (int c = 0; c < (int)rows[r].size(); ++c)
            drawCell(painter, r, c, rows[r][c]);

    // Cursor: block | bar | underline. The block cursor inverts the glyph
    // under it for contrast.
    if (cursorVisible) {
        int cr = grid.cursorRow(), cc = grid.cursorCol();
        if (cr >= 0 && cr < (int)rows.size() && cc >= 0 && cc < (int)rows[cr].size()) {
            int cx = cc * m_cellWidth, cy = cr * m_cellHeight;
            QColor cur = m_cursorColorSet ? m_cursorColor : m_defaultFg;
            if (m_cursorStyle == "bar") {
                painter.fillRect(cx, cy, 2, m_cellHeight, cur);
            } else if (m_cursorStyle == "underline") {
                painter.fillRect(cx, cy + m_cellHeight - 2, m_cellWidth, 2, cur);
            } else {  // block
                painter.fillRect(cx, cy, m_cellWidth, m_cellHeight, cur);
                const Cell& cell = rows[cr][cc];
                if (cell.ch != 0 && cell.ch != ' ') {
                    QColor under = ((cell.bg >> 24) == 0) ? m_defaultBg
                                                          : QColor::fromRgba(cell.bg);
                    painter.setFont(m_font);
                    painter.setPen(under);
                    char32_t cp = cell.ch;
                    painter.drawText(cx, cy + m_ascent, QString::fromUcs4(&cp, 1));
                }
            }
        }
    }
}

void QtRenderer::drawCell(QPainter& painter, int row, int col, const Cell& cell) {
    int x = col * m_cellWidth;
    int y = row * m_cellHeight;

    // cell.fg/bg are 0xAARRGGBB; alpha 0 means "use the terminal default".
    QColor bg = ((cell.bg >> 24) == 0) ? m_defaultBg : QColor::fromRgba(cell.bg);
    QColor fg = ((cell.fg >> 24) == 0) ? m_defaultFg : QColor::fromRgba(cell.fg);
    if (cell.attrs & ATTR_INVERSE) std::swap(fg, bg);

    painter.fillRect(x, y, m_cellWidth, m_cellHeight, bg);

    if (cell.ch != 0 && cell.ch != ' ') {
        if (cell.attrs & (ATTR_BOLD | ATTR_ITALIC | ATTR_UNDERLINE)) {
            QFont f = m_font;
            if (cell.attrs & ATTR_BOLD)      f.setBold(true);
            if (cell.attrs & ATTR_ITALIC)    f.setItalic(true);
            if (cell.attrs & ATTR_UNDERLINE) f.setUnderline(true);
            painter.setFont(f);
        } else {
            painter.setFont(m_font);
        }
        painter.setPen(fg);
        char32_t cp = cell.ch;
        painter.drawText(x, y + m_ascent, QString::fromUcs4(&cp, 1));
    } else if (cell.attrs & ATTR_UNDERLINE) {   // underline on a blank cell
        painter.setPen(fg);
        painter.drawLine(x, y + m_ascent + 1, x + m_cellWidth, y + m_ascent + 1);
    }
}