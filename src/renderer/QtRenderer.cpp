#include "brain/renderer/QtRenderer.hpp"
#include "brain/renderer/Cell.hpp"
#include <QPainter>
#include <QFontMetrics>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

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
    if (!file.open(QIODevice::ReadOnly)) return;

    auto doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return;
    auto obj = doc.object();

    if (obj.contains("foreground"))   m_defaultFg   = QColor(obj["foreground"].toString());
    if (obj.contains("background"))   m_defaultBg   = QColor(obj["background"].toString());
    if (obj.contains("selection_bg")) m_selectionBg = QColor(obj["selection_bg"].toString());
    if (obj.contains("cursor"))       m_cursorColor = QColor(obj["cursor"].toString());
}

void QtRenderer::render(QPainter& painter, const Grid& grid) {
    renderWithView(painter, grid, 0, nullptr, nullptr, nullptr, nullptr, true, true);
}

// Normalize a selection into top-left / bottom-right absolute coords.
static void normalizeSel(long long ar, int ac, long long fr, int fc,
                         long long& tr, int& tc, long long& br, int& bc) {
    if (ar < fr || (ar == fr && ac <= fc)) {
        tr = ar; tc = ac; br = fr; bc = fc;
    } else {
        tr = fr; tc = fc; br = ar; bc = ac;
    }
}

void QtRenderer::renderWithView(
    QPainter& painter,
    const Grid& grid,
    int viewportOffset,
    const long long* selAnchorAbsRow,
    const int*       selAnchorCol,
    const long long* selFocusAbsRow,
    const int*       selFocusCol,
    bool focused,
    bool cursorVisible)
{
    painter.setFont(m_font);

    // Fill the entire widget background first so blank rows past the grid
    // (e.g. while resizing larger) don't show garbage.
    painter.fillRect(painter.window(), m_defaultBg);

    const int visibleRows = grid.rowCount();
    const int cols        = grid.cols();
    const int histLines   = grid.historyLines();

    // Clamp viewport offset to the actual history we hold.
    int back = std::clamp(viewportOffset, 0, histLines);

    // Selection bounds in absolute rows.
    bool haveSel = selAnchorAbsRow && selAnchorCol && selFocusAbsRow && selFocusCol;
    long long selTopRow = 0, selBotRow = 0;
    int       selTopCol = 0, selBotCol = 0;
    if (haveSel) {
        normalizeSel(*selAnchorAbsRow, *selAnchorCol,
                     *selFocusAbsRow,  *selFocusCol,
                     selTopRow, selTopCol, selBotRow, selBotCol);
    }

    // Map: visible-row r (0..visibleRows-1) corresponds to absolute row
    //   absScroll + r          (when back == 0)
    //   absScroll - back + r   (in general)
    const long long topAbs = (long long)grid.absScroll() - back;

    auto cellAt = [&](int r) -> const std::vector<Cell>* {
        long long abs = topAbs + r;
        long long idx = abs - ((long long)grid.absScroll() - histLines);
        if (idx < 0) return nullptr;
        if (idx < histLines) return &grid.historyRow((int)idx);
        long long live = idx - histLines;
        if (live < visibleRows) return &grid.rows()[(int)live];
        return nullptr;
    };

    auto cellSelected = [&](long long abs, int c) {
        if (!haveSel) return false;
        if (abs < selTopRow || abs > selBotRow) return false;
        if (abs == selTopRow && c < selTopCol) return false;
        if (abs == selBotRow && c > selBotCol) return false;
        return true;
    };

    for (int r = 0; r < visibleRows; ++r) {
        const auto* row = cellAt(r);
        if (!row) continue;
        long long abs = topAbs + r;
        for (int c = 0; c < cols && c < (int)row->size(); ++c) {
            drawCell(painter, r, c, (*row)[c], cellSelected(abs, c));
        }
    }

    // Cursor — only when viewing the live tail (back == 0) and the visible
    // grid agrees the cursor is on-screen. Focused → solid, unfocused →
    // outlined. Style picks the shape (block/underline/bar).
    if (cursorVisible && back == 0) {
        int cr = grid.cursorRow();
        int cc = grid.cursorCol();
        if (cr >= 0 && cr < visibleRows && cc >= 0 && cc < cols) {
            int x = cc * m_cellWidth;
            int y = cr * m_cellHeight;
            QColor cur = m_cursorColor;

            if (focused) {
                painter.setPen(Qt::NoPen);
                if (m_cursorStyle == CursorBar) {
                    painter.fillRect(x, y, 2, m_cellHeight, cur);
                } else if (m_cursorStyle == CursorUnderline) {
                    painter.fillRect(x, y + m_cellHeight - 2,
                                     m_cellWidth, 2, cur);
                } else {
                    painter.fillRect(x, y, m_cellWidth, m_cellHeight, cur);
                    // Re-draw the glyph under a block cursor in the
                    // background colour for contrast.
                    const auto* row = cellAt(cr);
                    if (row && cc < (int)row->size() && (*row)[cc].ch > ' ') {
                        painter.setPen(m_defaultBg);
                        char32_t cp = (*row)[cc].ch;
                        painter.drawText(x, y + m_ascent,
                                         QString::fromUcs4(&cp, 1));
                    }
                }
            } else {
                // Unfocused — outline only, same shape as focused state.
                painter.setPen(cur);
                painter.setBrush(Qt::NoBrush);
                if (m_cursorStyle == CursorBar) {
                    painter.drawRect(x, y, 1, m_cellHeight - 1);
                } else if (m_cursorStyle == CursorUnderline) {
                    painter.drawRect(x, y + m_cellHeight - 2,
                                     m_cellWidth - 1, 1);
                } else {
                    painter.drawRect(x, y, m_cellWidth - 1, m_cellHeight - 1);
                }
            }
        }
    }
}

void QtRenderer::drawCell(QPainter& painter, int row, int col, const Cell& cell, bool selected) {
    int x = col * m_cellWidth;
    int y = row * m_cellHeight;

    QColor bg = ((cell.bg >> 24) == 0) ? m_defaultBg : QColor::fromRgba(cell.bg);
    QColor fg = ((cell.fg >> 24) == 0) ? m_defaultFg : QColor::fromRgba(cell.fg);

    if (cell.attrs & ATTR_INVERSE) std::swap(fg, bg);

    painter.fillRect(x, y, m_cellWidth, m_cellHeight, bg);
    if (selected) {
        painter.fillRect(x, y, m_cellWidth, m_cellHeight, m_selectionBg);
    }

    if (cell.ch != 0 && cell.ch != ' ') {
        QFont f = m_font;
        if (cell.attrs & ATTR_BOLD)   f.setBold(true);
        if (cell.attrs & ATTR_ITALIC) f.setItalic(true);
        painter.setFont(f);

        painter.setPen(fg);
        char32_t cp = cell.ch;
        painter.drawText(x, y + m_ascent, QString::fromUcs4(&cp, 1));

        if ((cell.attrs & ATTR_UNDERLINE) || cell.link != 0) {
            int uy = y + m_ascent + 2;
            if (uy > y + m_cellHeight - 1) uy = y + m_cellHeight - 1;
            painter.drawLine(x, uy, x + m_cellWidth, uy);
        }
    }
}
