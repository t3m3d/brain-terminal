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

// Is view cell (r,c) inside the normalized selection [sr0,sc0]..[sr1,sc1]?
static bool inSel(int r, int c, int sr0, int sc0, int sr1, int sc1) {
    if (r < sr0 || r > sr1) return false;
    if (r == sr0 && c < sc0) return false;
    if (r == sr1 && c > sc1) return false;
    return true;
}

void QtRenderer::render(QPainter& painter, const Grid& grid, bool cursorVisible,
                        int scrollOffset, bool selActive,
                        int sr0, int sc0, int sr1, int sc1) {
    painter.setFont(m_font);

    const auto& vis = grid.rows();
    int rowCount = (int)vis.size();
    int hist = grid.historyLines();
    int off = scrollOffset; if (off < 0) off = 0; if (off > hist) off = hist;

    // For each visible screen row, source from history when scrolled up.
    for (int r = 0; r < rowCount; ++r) {
        int globalLine = hist - off + r;
        const std::vector<Cell>* src;
        if (globalLine >= 0 && globalLine < hist) src = &grid.historyRow(globalLine);
        else {
            int vi = globalLine - hist;
            if (vi < 0 || vi >= rowCount) continue;
            src = &vis[vi];
        }
        const auto& cells = *src;
        for (int c = 0; c < (int)cells.size(); ++c)
            drawCell(painter, r, c, cells[c],
                     selActive && inSel(r, c, sr0, sc0, sr1, sc1));
    }

    // Cursor only in the live view (not while scrolled back).
    if (cursorVisible && off == 0) {
        const auto& rows = vis;
        int cr = grid.cursorRow(), cc = grid.cursorCol();
        if (cr >= 0 && cr < (int)rows.size() && cc >= 0 && cc < (int)rows[cr].size()) {
            int cx = m_padX + cc * m_cellWidth, cy = m_padY + cr * m_cellHeight;
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

void QtRenderer::drawCell(QPainter& painter, int row, int col, const Cell& cell, bool selected) {
    int x = m_padX + col * m_cellWidth;
    int y = m_padY + row * m_cellHeight;

    // cell.fg/bg are 0xAARRGGBB; alpha 0 means "use the terminal default".
    QColor bg = ((cell.bg >> 24) == 0) ? m_defaultBg : QColor::fromRgba(cell.bg);
    QColor fg = ((cell.fg >> 24) == 0) ? m_defaultFg : QColor::fromRgba(cell.fg);
    if (cell.attrs & ATTR_INVERSE) std::swap(fg, bg);
    if (selected) { bg = m_selBg; fg = m_selFg; }

    // Skip filling cells that match the default background - the paintEvent
    // base fill already covers them, and re-filling would double the alpha
    // when the background is semi-transparent.
    if (bg != m_defaultBg)
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