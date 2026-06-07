#include "brain/ui/TerminalWidget.hpp"
#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QGuiApplication>
#include <QClipboard>
#include <QFont>
#include <QColor>
#include <QFontMetrics>
#include <utility>
#include <algorithm>

namespace brain::ui {

TerminalWidget::TerminalWidget(const brain::Config& config, QWidget* parent)
    : QWidget(parent),
      m_config(config),
      m_terminal(80, 24),   // initial size; updated on resizeEvent
      m_input(),
      m_pty()
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(false);   // only track while a button is held (selection)
    m_fontSize = config.fontSize();
    if (config.opacityPercent() < 100) {
        setAttribute(Qt::WA_TranslucentBackground);
    }

    setupRenderer();
    setupPTY();

    // Terminal tells us when to repaint
    m_terminal.setRenderCallback([this]() {
        update();
    });

    // Terminal replies to the shell over the PTY (e.g. CSI 18t/14t size queries).
    m_terminal.setResponseCallback([this](const std::string& s) {
        m_pty.writeInput(s);
    });
    m_terminal.setCellPixels(m_cellWidth, m_cellHeight);

    // OSC 0/2 -> window title.
    m_terminal.setTitleCallback([this](const std::string& t) {
        if (auto* w = window()) w->setWindowTitle(QString::fromStdString(t));
    });
}

// ------------------------------------------------------------
// PTY setup -- uses config.shell()
// ------------------------------------------------------------
void TerminalWidget::setupPTY() {
    // PTY -> Terminal
    m_pty.setOutputCallback([this](const std::vector<char>& data) {
        m_terminal.onPTYOutput(data);
        m_scrollOffset = 0;   // jump to live view on new output
        update();
    });

    // Launch shell from config
    m_pty.spawnShell(m_config.shell());

    // Auto-run a startup command if the user configured one (e.g.
    // `startup_command = kryofetch`). Writes it as if the user typed
    // it at the prompt, followed by Enter. Skip if empty.
    const std::string& startup = m_config.startupCommand();
    if (!startup.empty()) {
        m_pty.writeInput(startup + "\r");
    }
}

// ------------------------------------------------------------
// Renderer setup -- uses config.font + theme
// ------------------------------------------------------------
void TerminalWidget::setupRenderer() {
    delete m_renderer; m_renderer = nullptr;
    // Build the font from config, but VERIFY Qt actually picked a fixed-
    // pitch face. Qt's font matcher silently falls back to the system
    // proportional font when the named family is absent (eg "Monospace"
    // doesn't exist on stock Windows -> falls back to Microsoft Sans
    // Serif, where 'M' is much wider than 'i'). The cell grid then uses
    // M's width and we get the "M i c r o s o f t" widely-spaced look.
    QFont font(QString::fromStdString(m_config.fontFamily()),
               m_fontSize);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);

    if (!QFontInfo(font).fixedPitch()) {
        // Walk a per-OS fallback list of known-monospace families until we
        // land on one that's actually installed.
        const QStringList candidates = {
#if defined(Q_OS_WIN)
            "Cascadia Mono", "Consolas", "Lucida Console", "Courier New",
#elif defined(Q_OS_MAC)
            "Menlo", "Monaco", "Courier New",
#else
            "DejaVu Sans Mono", "Liberation Mono", "Noto Sans Mono",
            "Ubuntu Mono", "Courier New",
#endif
            "Courier",
        };
        for (const QString& name : candidates) {
            QFont test(name, m_fontSize);
            test.setStyleHint(QFont::Monospace);
            test.setFixedPitch(true);
            if (QFontInfo(test).fixedPitch()) {
                font = test;
                break;
            }
        }
    }
    setFont(font);

    // Derive the cell size from the ACTUAL font metrics. For a true
    // monospace face every glyph has the same advance, so using 'M' is
    // safe; the fallback above guarantees we actually have one.
    QFontMetrics fm(font);
    m_cellWidth  = fm.horizontalAdvance(QChar('M'));
    m_cellHeight = fm.height();
    if (m_cellWidth  < 1) m_cellWidth  = 1;
    if (m_cellHeight < 1) m_cellHeight = 1;

    // Create renderer with the real cell dimensions
    m_renderer = new renderer::QtRenderer(font, m_cellWidth, m_cellHeight);

    // Load theme from config
    m_renderer->loadTheme(m_config.themePath());
    m_renderer->setCursorStyle(m_config.cursorStyle());

    // Config colours override the theme (0 = unset). Set BEFORE opacity below
    // so the alpha applies to the chosen background.
    if (m_config.foreground())  m_renderer->setDefaultFg(QColor::fromRgba(m_config.foreground()));
    if (m_config.background())  m_renderer->setDefaultBg(QColor::fromRgba(m_config.background()));
    if (m_config.cursorColor()) m_renderer->setCursorColor(QColor::fromRgba(m_config.cursorColor()));
    if (m_config.selectionBg() || m_config.selectionFg()) {
        m_renderer->setSelectionColors(
            m_config.selectionBg() ? QColor::fromRgba(m_config.selectionBg()) : QColor(0x44,0x44,0x66),
            m_config.selectionFg() ? QColor::fromRgba(m_config.selectionFg()) : QColor(0xFF,0xFF,0xFF));
    }
    for (int i = 0; i < 16; ++i)
        if (m_config.paletteColor(i)) m_terminal.setPaletteColor(i, m_config.paletteColor(i));
    m_renderer->setPadding(m_config.paddingX(), m_config.paddingY());

    // Bake window opacity into the default background alpha (Hyprland/Wayland
    // composites it). 100 = opaque.
    {
        int op = m_config.opacityPercent();
        if (op < 0) op = 0; if (op > 100) op = 100;
        QColor bg = m_renderer->defaultBg();
        bg.setAlpha(op * 255 / 100);
        m_renderer->setDefaultBg(bg);
    }
}

// ------------------------------------------------------------
// Paint event -- draw terminal grid
// ------------------------------------------------------------
void TerminalWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);

    if (m_renderer) {
        // Base fill establishes the (optionally semi-transparent) background.
        // CompositionMode_Source REPLACES pixels, so the transparent alpha
        // reaches the compositor and frames don't accumulate opacity.
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(rect(), m_renderer->defaultBg());
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

        // Normalize the selection so start precedes end in reading order.
        int sr0 = m_selStartRow, sc0 = m_selStartCol, sr1 = m_selEndRow, sc1 = m_selEndCol;
        if (sr0 > sr1 || (sr0 == sr1 && sc0 > sc1)) { std::swap(sr0, sr1); std::swap(sc0, sc1); }

        m_renderer->render(painter, m_terminal.grid(), m_terminal.cursorVisible(),
                           m_scrollOffset, m_hasSelection, sr0, sc0, sr1, sc1);
    }
}

// ------------------------------------------------------------
// Keyboard -> PTY
// ------------------------------------------------------------
void TerminalWidget::keyPressEvent(QKeyEvent* e) {
    const auto m = e->modifiers();
    const int  k = e->key();

    // ── shortcuts ────────────────────────────────────────────────────
    if ((m & Qt::ControlModifier) && (m & Qt::ShiftModifier)) {
        if (k == Qt::Key_C) { copySelectionToClipboard();  return; }
        if (k == Qt::Key_V) { pasteFromClipboard(false);   return; }
    }
    if (m & Qt::ControlModifier) {
        if (k == Qt::Key_Plus || k == Qt::Key_Equal) { applyFontSize(m_fontSize + 1);     return; }
        if (k == Qt::Key_Minus)                      { applyFontSize(m_fontSize - 1);     return; }
        if (k == Qt::Key_0)                          { applyFontSize(m_config.fontSize()); return; }
    }

    m_scrollOffset = 0;   // any key jumps back to the live view

    // ── special keys -> escape sequences ─────────────────────────────
    std::string out;
    switch (k) {
        case Qt::Key_Up:        out = "\x1b[A";  break;
        case Qt::Key_Down:      out = "\x1b[B";  break;
        case Qt::Key_Right:     out = "\x1b[C";  break;
        case Qt::Key_Left:      out = "\x1b[D";  break;
        case Qt::Key_Home:      out = "\x1b[H";  break;
        case Qt::Key_End:       out = "\x1b[F";  break;
        case Qt::Key_PageUp:    out = "\x1b[5~"; break;
        case Qt::Key_PageDown:  out = "\x1b[6~"; break;
        case Qt::Key_Insert:    out = "\x1b[2~"; break;
        case Qt::Key_Delete:    out = "\x1b[3~"; break;
        case Qt::Key_Return:
        case Qt::Key_Enter:     out = "\r";      break;
        case Qt::Key_Backspace: out = "\x7f";    break;
        case Qt::Key_Tab:       out = "\t";      break;
        case Qt::Key_Escape:    out = "\x1b";    break;
        default:
            if ((m & Qt::ControlModifier) && k >= Qt::Key_A && k <= Qt::Key_Z) {
                out = std::string(1, char(k - Qt::Key_A + 1));   // ^A..^Z control chars
            } else if (!e->text().isEmpty()) {
                out = e->text().toStdString();   // correct text: case, symbols, layout
            }
            break;
    }
    if (!out.empty()) m_pty.writeInput(out);
}

// ------------------------------------------------------------
// Resize terminal + renderer
// ------------------------------------------------------------
void TerminalWidget::resizeEvent(QResizeEvent*) {
    recomputeGrid();
}

// Recompute cols/rows from the current geometry, cell size and padding, and
// push the new dimensions to the terminal/renderer/PTY. Also used after a
// live font-size change.
void TerminalWidget::recomputeGrid() {
    int cols = (width()  - 2 * m_config.paddingX()) / m_cellWidth;
    int rows = (height() - 2 * m_config.paddingY()) / m_cellHeight;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;

    m_terminal.resize(cols, rows);
    if (m_renderer) m_renderer->resize(cols, rows);
    m_terminal.setCellPixels(m_cellWidth, m_cellHeight);
    m_pty.resize(cols, rows);
}

// ── live font-size (Ctrl +/-/0) ──────────────────────────────────────
void TerminalWidget::applyFontSize(int size) {
    if (size < 5)  size = 5;
    if (size > 96) size = 96;
    if (size == m_fontSize) return;
    m_fontSize = size;
    setupRenderer();     // rebuilds font + renderer at m_fontSize
    recomputeGrid();
    update();
}

// ── mouse selection ──────────────────────────────────────────────────
int TerminalWidget::rowAtY(int y) const {
    int r = (y - m_config.paddingY()) / m_cellHeight;
    if (r < 0) r = 0;
    int maxr = m_terminal.grid().rowCount() - 1;
    if (r > maxr) r = maxr;
    return r;
}
int TerminalWidget::colAtX(int x) const {
    int c = (x - m_config.paddingX()) / m_cellWidth;
    if (c < 0) c = 0;
    int maxc = m_terminal.grid().cols() - 1;
    if (c > maxc) c = maxc;
    return c;
}

void TerminalWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton) {
        pasteFromClipboard(true);   // X11/Wayland primary selection
        return;
    }
    if (e->button() == Qt::LeftButton) {
        m_selecting = true;
        m_hasSelection = false;
        m_selStartRow = m_selEndRow = rowAtY((int)e->position().y());
        m_selStartCol = m_selEndCol = colAtX((int)e->position().x());
        update();
    }
}

void TerminalWidget::mouseMoveEvent(QMouseEvent* e) {
    if (!m_selecting) return;
    m_selEndRow = rowAtY((int)e->position().y());
    m_selEndCol = colAtX((int)e->position().x());
    m_hasSelection = (m_selEndRow != m_selStartRow || m_selEndCol != m_selStartCol);
    update();
}

void TerminalWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) m_selecting = false;
}

// ── scrollback (mouse wheel) ─────────────────────────────────────────
void TerminalWidget::wheelEvent(QWheelEvent* e) {
    int steps = e->angleDelta().y() / 120;     // one notch = 120
    if (steps == 0) return;
    int hist = m_terminal.grid().historyLines();
    m_scrollOffset += steps * 3;               // 3 lines per notch
    if (m_scrollOffset < 0) m_scrollOffset = 0;
    if (m_scrollOffset > hist) m_scrollOffset = hist;
    update();
}

// ── clipboard ────────────────────────────────────────────────────────
void TerminalWidget::copySelectionToClipboard() {
    if (!m_hasSelection) return;
    int sr0 = m_selStartRow, sc0 = m_selStartCol, sr1 = m_selEndRow, sc1 = m_selEndCol;
    if (sr0 > sr1 || (sr0 == sr1 && sc0 > sc1)) { std::swap(sr0, sr1); std::swap(sc0, sc1); }

    const auto& grid = m_terminal.grid();
    int hist = grid.historyLines();
    int off  = m_scrollOffset;
    int rowCount = grid.rowCount();
    std::string text;
    for (int r = sr0; r <= sr1; ++r) {
        // map view row -> source cells (history when scrolled up)
        int globalLine = hist - off + r;
        const std::vector<renderer::Cell>* cells = nullptr;
        if (globalLine >= 0 && globalLine < hist) cells = &grid.historyRow(globalLine);
        else { int vi = globalLine - hist; if (vi >= 0 && vi < rowCount) cells = &grid.rows()[vi]; }
        if (!cells) continue;
        int c0 = (r == sr0) ? sc0 : 0;
        int c1 = (r == sr1) ? sc1 : (int)cells->size() - 1;
        std::string line;
        for (int c = c0; c <= c1 && c < (int)cells->size(); ++c) {
            uint32_t cp = (*cells)[c].ch;
            if (cp == 0) cp = ' ';
            // encode codepoint as UTF-8
            if (cp < 0x80) line += (char)cp;
            else if (cp < 0x800) { line += (char)(0xC0 | (cp >> 6)); line += (char)(0x80 | (cp & 0x3F)); }
            else if (cp < 0x10000) { line += (char)(0xE0 | (cp >> 12)); line += (char)(0x80 | ((cp >> 6) & 0x3F)); line += (char)(0x80 | (cp & 0x3F)); }
            else { line += (char)(0xF0 | (cp >> 18)); line += (char)(0x80 | ((cp >> 12) & 0x3F)); line += (char)(0x80 | ((cp >> 6) & 0x3F)); line += (char)(0x80 | (cp & 0x3F)); }
        }
        // trim trailing spaces on each line
        while (!line.empty() && line.back() == ' ') line.pop_back();
        if (r != sr0) text += '\n';
        text += line;
    }
    if (auto* cb = QGuiApplication::clipboard()) {
        cb->setText(QString::fromStdString(text), QClipboard::Clipboard);
        if (cb->supportsSelection())
            cb->setText(QString::fromStdString(text), QClipboard::Selection);
    }
}

void TerminalWidget::pasteFromClipboard(bool primary) {
    auto* cb = QGuiApplication::clipboard();
    if (!cb) return;
    QString s = cb->text(primary && cb->supportsSelection()
                             ? QClipboard::Selection : QClipboard::Clipboard);
    if (s.isEmpty()) return;
    std::string data = s.toStdString();
    m_scrollOffset = 0;
    if (m_terminal.bracketedPaste())
        m_pty.writeInput("\x1b[200~" + data + "\x1b[201~");
    else
        m_pty.writeInput(data);
}

} // namespace brain::ui