#include "brain/renderer/QtRenderer.hpp"
#include "brain/renderer/Cell.hpp"
#include <QPainter>
#include <QFontMetrics>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QImage>
#include <algorithm>
#include <utility>

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

    if (obj.contains("foreground"))   m_defaultFg = QColor(obj["foreground"].toString());
    if (obj.contains("background"))   m_defaultBg = QColor(obj["background"].toString());
    if (obj.contains("selection_bg")) m_selBg     = QColor(obj["selection_bg"].toString());
    if (obj.contains("cursor"))     { m_cursorColor = QColor(obj["cursor"].toString()); m_cursorColorSet = true; }

    // Optional 16-entry ANSI palette ("palette": ["#rrggbb", ... x16]).
    if (obj.contains("palette") && obj["palette"].isArray()) {
        auto arr = obj["palette"].toArray();
        for (int i = 0; i < 16 && i < arr.size(); ++i) {
            QColor c(arr[i].toString());
            if (c.isValid()) { m_themePalette[i] = c.rgba(); m_themePaletteSet[i] = true; }
        }
    }
}


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
    bool cursorVisible,
    const std::vector<TermImage>* images)
{
    painter.setFont(m_font);

    // Background. Honour padding so the widget edges fill cleanly even
    // when m_padX/m_padY > 0 (opaque background only; semi-transparent
    // bg is handled by the window's opacity setting).
    painter.fillRect(painter.window(), m_defaultBg);

    const int visibleRows = grid.rowCount();
    const int cols        = grid.cols();
    const int histLines   = grid.historyLines();

    int back = std::clamp(viewportOffset, 0, histLines);

    bool haveSel = selAnchorAbsRow && selAnchorCol && selFocusAbsRow && selFocusCol;
    long long selTopRow = 0, selBotRow = 0;
    int       selTopCol = 0, selBotCol = 0;
    if (haveSel) {
        normalizeSel(*selAnchorAbsRow, *selAnchorCol,
                     *selFocusAbsRow,  *selFocusCol,
                     selTopRow, selTopCol, selBotRow, selBotCol);
    }

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

    // Two-pass row rendering so glyphs that overflow their cell (powerline
    // separators, ligatures, wide Nerd-Font icons) survive instead of being
    // clipped by the next cell's bg fill. Pass 1: all bgs. Pass 2: all text.
    for (int r = 0; r < visibleRows; ++r) {
        const auto* row = cellAt(r);
        if (!row) continue;
        long long abs = topAbs + r;
        int rowCols = std::min(cols, (int)row->size());

        // Pass 1: backgrounds + selection overlays.
        for (int c = 0; c < rowCols; ++c) {
            const Cell& cell = (*row)[c];
            int x = m_padX + c * m_cellWidth;
            int y = m_padY + r * m_cellHeight;
            QColor bg = ((cell.bg >> 24) == 0) ? m_defaultBg : QColor::fromRgba(cell.bg);
            QColor fg = ((cell.fg >> 24) == 0) ? m_defaultFg : QColor::fromRgba(cell.fg);
            if (cell.attrs & ATTR_INVERSE) std::swap(fg, bg);
            if (bg != m_defaultBg)
                painter.fillRect(x, y, m_cellWidth, m_cellHeight, bg);
            if (cellSelected(abs, c))
                painter.fillRect(x, y, m_cellWidth, m_cellHeight, m_selBg);
        }

        // Pass 2: glyphs. Inlined (a trimmed-down drawCell that SKIPS the bg
        // fill, since pass 1 did it) so overflow from cell N isn't clipped by
        // cell N+1's bg fill.
        for (int c = 0; c < rowCols; ++c) {
            const Cell& cell = (*row)[c];
            if (cell.ch == 0 || cell.ch == ' ') {
                if ((cell.attrs & ATTR_UNDERLINE) || cell.link != 0) {
                    int x = m_padX + c * m_cellWidth;
                    int y = m_padY + r * m_cellHeight;
                    QColor fg = ((cell.fg >> 24) == 0) ? m_defaultFg : QColor::fromRgba(cell.fg);
                    if (cell.attrs & ATTR_INVERSE) fg = m_defaultBg;
                    int style = (cell.attrs & ATTR_UNDERLINE) ? cell.ulStyle : 0;
                    QColor ulc = ((cell.attrs & ATTR_UNDERLINE) && cell.ulColor)
                               ? QColor::fromRgba(cell.ulColor) : fg;
                    drawUnderline(painter, x, y, m_cellWidth, style, ulc);
                }
                continue;
            }
            int x = m_padX + c * m_cellWidth;
            int y = m_padY + r * m_cellHeight;
            QColor bg = ((cell.bg >> 24) == 0) ? m_defaultBg : QColor::fromRgba(cell.bg);
            QColor fg = ((cell.fg >> 24) == 0) ? m_defaultFg : QColor::fromRgba(cell.fg);
            if (cell.attrs & ATTR_INVERSE) std::swap(fg, bg);
            if (cell.attrs & ATTR_DIM)     fg = fg.darker(160);   // SGR 2 faint

            bool wantBold = m_useBold && (cell.attrs & ATTR_BOLD)
                         && (m_font.weight() < QFont::DemiBold);
            if (wantBold || (cell.attrs & (ATTR_ITALIC | ATTR_STRIKE))) {
                QFont f = m_font;
                if (wantBold)                  f.setBold(true);
                if (cell.attrs & ATTR_ITALIC)  f.setItalic(true);
                if (cell.attrs & ATTR_STRIKE)  f.setStrikeOut(true);
                painter.setFont(f);
            } else {
                painter.setFont(m_font);
            }
            painter.setPen(fg);
            char32_t cp = cell.ch;
            painter.drawText(x, y + m_ascent,
                             QString::fromUcs4(reinterpret_cast<const char32_t*>(&cp), 1));

            // Underline (styled: single/double/curly/dotted/dashed) drawn by
            // hand so undercurl etc. work; SGR 58 colours it, else use fg. OSC 8
            // links get a plain underline.
            if (cell.attrs & ATTR_UNDERLINE)
                drawUnderline(painter, x, y, m_cellWidth, cell.ulStyle,
                              cell.ulColor ? QColor::fromRgba(cell.ulColor) : fg);
            else if (cell.link != 0)
                drawUnderline(painter, x, y, m_cellWidth, 0, fg);
        }
    }

    // Inline images (Sixel): blit anchored bitmaps that intersect the viewport,
    // mapped through the same abs->screen-row math as the cells.
    if (images && !images->empty()) {
        const int winH = painter.window().height();
        for (const auto& im : *images) {
            if (im.wpx <= 0 || im.hpx <= 0 || (int)im.argb.size() < im.wpx * im.hpx)
                continue;
            long long screenRow = im.anchorAbs - topAbs;
            int y = m_padY + (int)screenRow * m_cellHeight;
            int x = m_padX + im.col * m_cellWidth;
            if (y + im.hpx <= 0 || y >= winH) continue;   // fully off-screen
            QImage qi(reinterpret_cast<const uchar*>(im.argb.data()),
                      im.wpx, im.hpx, im.wpx * 4, QImage::Format_ARGB32);
            painter.drawImage(QPoint(x, y), qi);
        }
    }

    if (cursorVisible && back == 0) {
        int cr = grid.cursorRow();
        int cc = grid.cursorCol();
        if (cr >= 0 && cr < visibleRows && cc >= 0 && cc < cols) {
            int x = m_padX + cc * m_cellWidth;
            int y = m_padY + cr * m_cellHeight;
            QColor cur = m_cursorColorSet ? m_cursorColor : m_defaultFg;

            if (focused) {
                painter.setPen(Qt::NoPen);
                if (m_cursorStyle == "bar") {
                    painter.fillRect(x, y, 2, m_cellHeight, cur);
                } else if (m_cursorStyle == "underline") {
                    painter.fillRect(x, y + m_cellHeight - 2, m_cellWidth, 2, cur);
                } else {
                    painter.fillRect(x, y, m_cellWidth, m_cellHeight, cur);
                    const auto* row = cellAt(cr);
                    if (row && cc < (int)row->size() && (*row)[cc].ch > ' ') {
                        painter.setPen(m_defaultBg);
                        char32_t cp = (*row)[cc].ch;
                        painter.drawText(x, y + m_ascent,
                                         QString::fromUcs4(reinterpret_cast<const char32_t*>(&cp), 1));
                    }
                }
            } else {
                painter.setPen(cur);
                painter.setBrush(Qt::NoBrush);
                if (m_cursorStyle == "bar") {
                    painter.drawRect(x, y, 1, m_cellHeight - 1);
                } else if (m_cursorStyle == "underline") {
                    painter.drawRect(x, y + m_cellHeight - 2, m_cellWidth - 1, 1);
                } else {
                    painter.drawRect(x, y, m_cellWidth - 1, m_cellHeight - 1);
                }
            }
        }
    }
}

void QtRenderer::drawUnderline(QPainter& painter, int x, int y, int w, int style, const QColor& col) {
    int uy = y + m_ascent + 2;
    if (uy > y + m_cellHeight - 2) uy = y + m_cellHeight - 2;
    QPen pen(col);
    pen.setWidth(1);
    switch (style) {
        case UL_DOUBLE:
            painter.setPen(pen);
            painter.drawLine(x, uy - 1, x + w, uy - 1);
            painter.drawLine(x, uy + 1, x + w, uy + 1);
            break;
        case UL_CURLY: {                 // zig-zag approximation of undercurl
            painter.setPen(pen);
            int amp = 1, step = 2, py = uy + amp;
            bool up = true;
            for (int px = x; px < x + w; px += step) {
                int nx = std::min(px + step, x + w);
                int ny = up ? uy - amp : uy + amp;
                painter.drawLine(px, py, nx, ny);
                py = ny; up = !up;
            }
            break;
        }
        case UL_DOTTED: pen.setStyle(Qt::DotLine);  painter.setPen(pen); painter.drawLine(x, uy, x + w, uy); break;
        case UL_DASHED: pen.setStyle(Qt::DashLine); painter.setPen(pen); painter.drawLine(x, uy, x + w, uy); break;
        default:        painter.setPen(pen);        painter.drawLine(x, uy, x + w, uy); break;  // single
    }
}

