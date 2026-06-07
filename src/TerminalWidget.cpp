#include "brain/ui/TerminalWidget.hpp"
#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFocusEvent>
#include <QFont>
#include <QFontMetrics>
#include <QTimer>
#include <QApplication>
#include <QClipboard>
#include <QStringList>
#include <QDesktopServices>
#include <QUrl>
#include <QRegularExpression>
#include <algorithm>

namespace brain::ui {

TerminalWidget::TerminalWidget(const brain::Config& config, QWidget* parent)
    : QWidget(parent),
      m_config(config),
      m_terminal(80, 24),
      m_input(),
      m_pty()
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(false);   // moveEvents only while a button is held
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    // Honour user-configured scrollback before any PTY output arrives.
    // 0 disables scrollback entirely (matches "set --no-scrollback" UX).
    if (m_config.scrollback() >= 0) m_terminal.setScrollback(m_config.scrollback());

    setupRenderer();
    setupPTY();
    hookTerminalSignals();

    m_terminal.setRenderCallback([this]() {
        update();
    });
}

void TerminalWidget::hookTerminalSignals() {
    m_terminal.setTitleCallback([this](const std::string& t) {
        emit titleChanged(QString::fromStdString(t));
    });
    m_terminal.setBellCallback([this]() {
        emit bellRang();
        // Visual flash: invert the widget for one paint cycle. We don't
        // hold a flag because the renderer doesn't need one — instead
        // the window can listen for bellRang() and flash the chrome.
        update();
    });
}

// ---------------------------------------------------------------------------
// PTY setup
// ---------------------------------------------------------------------------
void TerminalWidget::setupPTY() {
    // Track whether we've ever seen output. The startup_command write
    // gets deferred until the shell has actually painted something —
    // otherwise we race the first prompt and kryofetch's clear-screen
    // wipes the prompt's terminator, leaving 3-4 lines visible scroll.
    auto firstOutputSeen = std::make_shared<bool>(false);

    m_pty.setOutputCallback([this, firstOutputSeen](const std::vector<char>& data) {
        m_terminal.onPTYOutput(data);

        if (!*firstOutputSeen && !data.empty()) {
            *firstOutputSeen = true;

            const std::string startup = m_config.startupCommand();
            if (!startup.empty()) {
                // Give the shell one more beat to finish painting the
                // prompt after its banner. cmd.exe paints banner +
                // prompt in two separate writes; 120 ms covers the gap
                // between them on every Windows version we've tested.
                QTimer::singleShot(120, this, [this, startup]() {
                    m_pty.writeInput(startup + "\r");
                });
            }
        }

        update();
    });

    m_pty.spawnShell(m_config.shell());
}

// ---------------------------------------------------------------------------
// Renderer setup
// ---------------------------------------------------------------------------
void TerminalWidget::setupRenderer() {
    QFont font(QString::fromStdString(m_config.fontFamily()),
               m_config.fontSize());
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);

    if (!QFontInfo(font).fixedPitch()) {
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
            QFont test(name, m_config.fontSize());
            test.setStyleHint(QFont::Monospace);
            test.setFixedPitch(true);
            if (QFontInfo(test).fixedPitch()) {
                font = test;
                break;
            }
        }
    }
    setFont(font);

    QFontMetrics fm(font);
    m_cellWidth  = fm.horizontalAdvance(QChar('M'));
    m_cellHeight = fm.height();
    if (m_cellWidth  < 1) m_cellWidth  = 1;
    if (m_cellHeight < 1) m_cellHeight = 1;

    m_renderer = new renderer::QtRenderer(font, m_cellWidth, m_cellHeight);
    m_renderer->loadTheme(m_config.themePath());

    // cursor_style ∈ {block, underline, bar}. Default block.
    const std::string& cs = m_config.cursorStyle();
    if      (cs == "underline") m_renderer->setCursorStyle(renderer::QtRenderer::CursorUnderline);
    else if (cs == "bar")       m_renderer->setCursorStyle(renderer::QtRenderer::CursorBar);
    else                        m_renderer->setCursorStyle(renderer::QtRenderer::CursorBlock);
}

// ---------------------------------------------------------------------------
// Paint
// ---------------------------------------------------------------------------
void TerminalWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);

    if (m_renderer) {
        m_renderer->renderWithView(
            painter,
            m_terminal.grid(),
            m_viewportOffset,
            m_hasSelection ? &m_selAnchor.absRow : nullptr,
            m_hasSelection ? &m_selAnchor.col    : nullptr,
            m_hasSelection ? &m_selFocus.absRow  : nullptr,
            m_hasSelection ? &m_selFocus.col     : nullptr,
            m_focused,
            m_terminal.cursorVisible());
    }
}

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------
void TerminalWidget::keyPressEvent(QKeyEvent* e) {
    using namespace brain::input;

    if ((e->modifiers() & Qt::ControlModifier) &&
        (e->modifiers() & Qt::ShiftModifier)) {
        if (e->key() == Qt::Key_C) { copySelectionToClipboard(); return; }
        if (e->key() == Qt::Key_V) { pasteFromClipboard();       return; }
    }

    Modifier mod = Modifier::None;
    if      (e->modifiers() & Qt::ControlModifier) mod = Modifier::Ctrl;
    else if (e->modifiers() & Qt::AltModifier)     mod = Modifier::Alt;
    else if (e->modifiers() & Qt::ShiftModifier)   mod = Modifier::Shift;

    std::string text = e->text().toStdString();
    std::string seq  = m_input.translate(e->key(), mod, text);
    if (!seq.empty()) {
        m_pty.writeInput(seq);
        if (m_viewportOffset != 0) {
            m_viewportOffset = 0;
            update();
        }
    }
}

// ---------------------------------------------------------------------------
// Resize
// ---------------------------------------------------------------------------
void TerminalWidget::resizeEvent(QResizeEvent*) {
    int cols = width() / m_cellWidth;
    int rows = height() / m_cellHeight;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;

    m_terminal.resize(cols, rows);
    if (m_renderer) m_renderer->resize(cols, rows);
    m_pty.resize(cols, rows);
}

// ---------------------------------------------------------------------------
// Mouse
// ---------------------------------------------------------------------------
TerminalWidget::SelPoint TerminalWidget::pixelToCell(const QPoint& p) const {
    SelPoint sp;
    int col = p.x() / m_cellWidth;
    int row = p.y() / m_cellHeight;
    col = std::clamp(col, 0, m_terminal.grid().cols() - 1);
    row = std::clamp(row, 0, m_terminal.grid().rowCount() - 1);

    // Translate the visible row into an absolute row. With scrollback, the
    // top visible row corresponds to:
    //   absScroll() - m_viewportOffset
    long long topAbs = (long long)m_terminal.grid().absScroll() - m_viewportOffset;
    sp.absRow = topAbs + row;
    sp.col    = col;
    return sp;
}

// URL pattern. Conservative — http(s)/ftp/file schemes and anything that
// looks domain-y. Avoids the punctuation-spillover pitfall by trimming
// trailing .,;:!?)]>" once the regex matches.
static QString urlAt(const QString& rowText, int col) {
    static const QRegularExpression re(
        R"((?:https?|ftp|file)://[^\s)>\]"<>]+)",
        QRegularExpression::CaseInsensitiveOption);
    auto it = re.globalMatch(rowText);
    while (it.hasNext()) {
        auto m = it.next();
        if (col >= m.capturedStart() && col < m.capturedEnd()) {
            QString u = m.captured();
            while (!u.isEmpty()) {
                QChar tail = u.back();
                if (tail == '.' || tail == ',' || tail == ';' ||
                    tail == ':' || tail == '!' || tail == '?' ||
                    tail == ')' || tail == ']' || tail == '"') {
                    u.chop(1);
                } else break;
            }
            return u;
        }
    }
    return {};
}

void TerminalWidget::mousePressEvent(QMouseEvent* e) {
    // Ctrl+click → open URL under cursor in the default browser. No
    // visual underline yet; that ships with OSC 8 in a follow-up.
    if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ControlModifier)) {
        SelPoint sp = pixelToCell(e->pos());
        int row = sp.absRow - ((long long)m_terminal.grid().absScroll() - m_viewportOffset);
        if (row >= 0 && row < m_terminal.grid().rowCount()) {
            const auto& cells = m_terminal.grid().rows()[row];
            QString text;
            for (const auto& cell : cells) {
                uint32_t cp = cell.ch ? cell.ch : ' ';
                text += QString::fromUcs4(reinterpret_cast<const char32_t*>(&cp), 1);
            }
            QString u = urlAt(text, sp.col);
            if (!u.isEmpty()) {
                QDesktopServices::openUrl(QUrl(u));
                return;   // do NOT start a selection on this click
            }
        }
    }

    if (e->button() != Qt::LeftButton) return;
    m_selAnchor = pixelToCell(e->pos());
    m_selFocus  = m_selAnchor;
    m_selecting = true;
    m_hasSelection = false;
    setMouseTracking(true);
    update();
}

void TerminalWidget::mouseMoveEvent(QMouseEvent* e) {
    if (!m_selecting) return;
    m_selFocus = pixelToCell(e->pos());
    m_hasSelection = (m_selFocus.absRow != m_selAnchor.absRow
                   || m_selFocus.col    != m_selAnchor.col);
    update();
}

void TerminalWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    m_selecting = false;
    setMouseTracking(false);
}

void TerminalWidget::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    // Word-select: walk the row from the click outward to the nearest
    // whitespace.
    SelPoint sp = pixelToCell(e->pos());
    int row = sp.absRow - ((long long)m_terminal.grid().absScroll() - m_viewportOffset);
    if (row < 0 || row >= m_terminal.grid().rowCount()) return;
    const auto& cells = m_terminal.grid().rows()[row];
    auto isWord = [](uint32_t cp) {
        return cp > ' ' && cp != 0x7f;
    };
    int a = sp.col, b = sp.col;
    while (a > 0 && isWord(cells[a - 1].ch)) --a;
    while (b + 1 < (int)cells.size() && isWord(cells[b + 1].ch)) ++b;
    m_selAnchor = { sp.absRow, a };
    m_selFocus  = { sp.absRow, b };
    m_hasSelection = true;
    update();
}

void TerminalWidget::wheelEvent(QWheelEvent* e) {
    // 120 angle-delta units = one notch on most mice = 3 lines (matches xterm).
    int notches = e->angleDelta().y() / 120;
    if (notches == 0) {
        // Trackpads emit small angle deltas; floor them so users still scroll.
        notches = (e->angleDelta().y() > 0) ? 1 : -1;
    }
    int delta = notches * 3;

    int newOffset = m_viewportOffset + delta;
    int maxBack   = m_terminal.grid().historyLines();
    newOffset = std::clamp(newOffset, 0, maxBack);

    if (newOffset != m_viewportOffset) {
        m_viewportOffset = newOffset;
        update();
    }
    e->accept();
}

// ---------------------------------------------------------------------------
// Focus
// ---------------------------------------------------------------------------
void TerminalWidget::focusInEvent(QFocusEvent*)  { m_focused = true;  update(); }
void TerminalWidget::focusOutEvent(QFocusEvent*) { m_focused = false; update(); }

// ---------------------------------------------------------------------------
// Clipboard
// ---------------------------------------------------------------------------
QString TerminalWidget::selectionText() const {
    if (!m_hasSelection) return {};

    // Normalize: walk top-left -> bottom-right.
    SelPoint a = m_selAnchor, b = m_selFocus;
    if (a.absRow > b.absRow || (a.absRow == b.absRow && a.col > b.col))
        std::swap(a, b);

    const auto& grid = m_terminal.grid();
    long long topAbs = grid.absScroll() - grid.historyLines();
    int totalHistRows = grid.historyLines();

    auto rowAt = [&](long long absRow) -> const std::vector<renderer::Cell>* {
        long long idx = absRow - topAbs;
        if (idx < 0) return nullptr;
        if (idx < totalHistRows) return &grid.historyRow((int)idx);
        long long live = idx - totalHistRows;
        if (live < grid.rowCount()) return &grid.rows()[(int)live];
        return nullptr;
    };

    QString out;
    for (long long r = a.absRow; r <= b.absRow; ++r) {
        const auto* row = rowAt(r);
        if (!row) continue;
        int startC = (r == a.absRow) ? a.col : 0;
        int endC   = (r == b.absRow) ? b.col : (int)row->size() - 1;
        if (startC < 0) startC = 0;
        if (endC >= (int)row->size()) endC = (int)row->size() - 1;

        // Trim trailing blanks to avoid stripes of spaces in the copy.
        int lastNonBlank = startC - 1;
        for (int c = startC; c <= endC; ++c)
            if ((*row)[c].ch > ' ') lastNonBlank = c;

        for (int c = startC; c <= lastNonBlank; ++c) {
            uint32_t cp = (*row)[c].ch;
            if (cp == 0) cp = ' ';
            out += QString::fromUcs4(reinterpret_cast<const char32_t*>(&cp), 1);
        }
        if (r != b.absRow) out += '\n';
    }
    return out;
}

void TerminalWidget::copySelectionToClipboard() {
    QString t = selectionText();
    if (t.isEmpty()) return;
    QApplication::clipboard()->setText(t);
}

void TerminalWidget::pasteFromClipboard() {
    QString t = QApplication::clipboard()->text();
    if (t.isEmpty()) return;
    // Normalize line endings — most shells want \r, not \r\n / \n.
    t.replace(QStringLiteral("\r\n"), QStringLiteral("\r"));
    t.replace('\n', '\r');

    std::string s = t.toStdString();
    if (m_terminal.bracketedPaste()) {
        m_pty.writeInput(std::string("\x1b[200~") + s + "\x1b[201~");
    } else {
        m_pty.writeInput(s);
    }
    if (m_viewportOffset != 0) {
        m_viewportOffset = 0;
        update();
    }
}

} // namespace brain::ui
