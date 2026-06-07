#include "brain/ui/TerminalWidget.hpp"
#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFocusEvent>
#include <QFont>
#include <QColor>
#include <QFontMetrics>
#include <QTimer>
#include <QApplication>
#include <QClipboard>
#include <QStringList>
#include <QDesktopServices>
#include <QUrl>
#include <QRegularExpression>
#include <QLineEdit>
#include <QLabel>
#include <QHBoxLayout>
#include <QFrame>
#include <QResizeEvent>
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
    setMouseTracking(false);

    // Opaque paint by default (fast path). When the user wants
    // transparency, switch on the translucent flag — the renderer's bg
    // fill won't double-alpha because cells skip filling when bg matches
    // the default.
    if (config.opacityPercent() < 100)
        setAttribute(Qt::WA_TranslucentBackground);
    else
        setAttribute(Qt::WA_OpaquePaintEvent, true);

    if (m_config.scrollback() >= 0)
        m_terminal.setScrollback(m_config.scrollback());

    setupRenderer();
    setupPTY();
    hookTerminalSignals();

    m_terminal.setRenderCallback([this]() {
        update();
    });

    // Terminal replies to the shell over the PTY (e.g. CSI 18t/14t size queries).
    m_terminal.setResponseCallback([this](const std::string& s) {
        m_pty.writeInput(s);
    });
    m_terminal.setCellPixels(m_cellWidth, m_cellHeight);
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
    m_renderer->setCursorStyle(m_config.cursorStyle());
    m_currentFontSize = m_config.fontSize();

    // Config colour overrides (0 = unset).
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

    // Bake window opacity into the default background alpha (compositors —
    // Hyprland/Wayland/DWM — composite this). 100 = opaque.
    {
        int op = m_config.opacityPercent();
        if (op < 0) op = 0; if (op > 100) op = 100;
        QColor bg = m_renderer->defaultBg();
        bg.setAlpha(op * 255 / 100);
        m_renderer->setDefaultBg(bg);
    }
}

// ---------------------------------------------------------------------------
// Paint
// ---------------------------------------------------------------------------
void TerminalWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);

    if (m_renderer) {
        // Base fill establishes the (optionally semi-transparent) background.
        // CompositionMode_Source REPLACES pixels, so transparent alpha reaches
        // the compositor and successive frames don't accumulate opacity.
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(rect(), m_renderer->defaultBg());
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

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

    if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_F) {
        openFindBar();
        return;
    }

    // Ctrl + / Ctrl - / Ctrl 0 — live font-size adjust (zoom).
    if (e->modifiers() & Qt::ControlModifier) {
        int k = e->key();
        if (k == Qt::Key_Plus || k == Qt::Key_Equal) { applyFontSize(m_currentFontSize + 1); return; }
        if (k == Qt::Key_Minus)                      { applyFontSize(m_currentFontSize - 1); return; }
        if (k == Qt::Key_0)                          { applyFontSize(m_config.fontSize()); return; }
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
    int cols = (width()  - 2 * m_config.paddingX()) / m_cellWidth;
    int rows = (height() - 2 * m_config.paddingY()) / m_cellHeight;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;

    m_terminal.resize(cols, rows);
    if (m_renderer) m_renderer->resize(cols, rows);
    m_pty.resize(cols, rows);
    positionFindBar();
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

static int mouseMods(Qt::KeyboardModifiers m) {
    int v = 0;
    if (m & Qt::ShiftModifier)   v += 4;
    if (m & Qt::AltModifier)     v += 8;
    if (m & Qt::ControlModifier) v += 16;
    return v;
}

// Screen (1-based) col/row of a mouse event, for mouse reporting. Returns
// false if the click is above the live grid (scrolled back).
bool TerminalWidget::reportMouse(QMouseEvent* e, int button, bool press, bool motion) {
    if (m_terminal.mouseMode() == 0) return false;
    if (e->modifiers() & Qt::ShiftModifier) return false;   // Shift = local select
    SelPoint sp = pixelToCell(e->pos());
    int viewRow = (int)(sp.absRow -
                  ((long long)m_terminal.grid().absScroll() - m_viewportOffset));
    if (viewRow < 0 || viewRow >= m_terminal.grid().rowCount()) return false;
    m_pty.writeInput(m_terminal.mouseReport(button, sp.col + 1, viewRow + 1,
                                            press, motion, mouseMods(e->modifiers())));
    return true;
}

static int qtButtonCode(Qt::MouseButton b) {
    return b == Qt::LeftButton ? 0 : b == Qt::MiddleButton ? 1 : b == Qt::RightButton ? 2 : 0;
}

void TerminalWidget::mousePressEvent(QMouseEvent* e) {
    // App mouse reporting consumes the click (Shift forces local selection).
    if (reportMouse(e, qtButtonCode(e->button()), true, false)) return;

    // Ctrl+click → open URL. First check the cell's OSC 8 link id (set
    // by ls --hyperlink, gh, git, etc), then fall back to a regex scan
    // of the row text.
    if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ControlModifier)) {
        SelPoint sp = pixelToCell(e->pos());
        int row = sp.absRow - ((long long)m_terminal.grid().absScroll() - m_viewportOffset);
        if (row >= 0 && row < m_terminal.grid().rowCount()) {
            const auto& cells = m_terminal.grid().rows()[row];
            if (sp.col >= 0 && sp.col < (int)cells.size()) {
                uint16_t link = cells[sp.col].link;
                if (link) {
                    const std::string& uri = m_terminal.linkUri(link);
                    if (!uri.empty()) {
                        QDesktopServices::openUrl(QUrl(QString::fromStdString(uri)));
                        return;
                    }
                }
            }
            QString text;
            for (const auto& cell : cells) {
                uint32_t cp = cell.ch ? cell.ch : ' ';
                text += QString::fromUcs4(reinterpret_cast<const char32_t*>(&cp), 1);
            }
            QString u = urlAt(text, sp.col);
            if (!u.isEmpty()) {
                QDesktopServices::openUrl(QUrl(u));
                return;
            }
        }
    }

    // Middle-click → primary-selection paste (X11/Wayland convention).
    if (e->button() == Qt::MiddleButton) {
        pasteFromClipboard(true);
        return;
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
    // Drag reporting (1002/1003) while a button is held.
    if (m_terminal.mouseMode() >= 1002 && e->buttons() != Qt::NoButton) {
        int b = (e->buttons() & Qt::LeftButton) ? 0 :
                (e->buttons() & Qt::MiddleButton) ? 1 :
                (e->buttons() & Qt::RightButton) ? 2 : 0;
        if (reportMouse(e, b, true, true)) return;
    }
    if (!m_selecting) return;
    m_selFocus = pixelToCell(e->pos());
    m_hasSelection = (m_selFocus.absRow != m_selAnchor.absRow
                   || m_selFocus.col    != m_selAnchor.col);
    update();
}

void TerminalWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (reportMouse(e, qtButtonCode(e->button()), false, false)) return;
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

    // While an app reports the mouse and we're at the live tail, send the
    // wheel as button 64 (up) / 65 (down) instead of scrolling our scrollback.
    if (m_terminal.mouseMode() > 0 && m_viewportOffset == 0
        && !(e->modifiers() & Qt::ShiftModifier)) {
        SelPoint sp = pixelToCell(e->position().toPoint());
        int viewRow = (int)(sp.absRow -
                      ((long long)m_terminal.grid().absScroll() - m_viewportOffset));
        if (viewRow >= 0) {
            int b = notches > 0 ? 64 : 65;
            for (int i = 0; i < std::abs(notches); ++i)
                m_pty.writeInput(m_terminal.mouseReport(b, sp.col + 1, viewRow + 1,
                                                        true, false, mouseMods(e->modifiers())));
            return;
        }
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
    QClipboard* cb = QApplication::clipboard();
    cb->setText(t);
    if (cb->supportsSelection())
        cb->setText(t, QClipboard::Selection);
}

void TerminalWidget::pasteFromClipboard(bool primary) {
    QClipboard::Mode mode = primary ? QClipboard::Selection : QClipboard::Clipboard;
    if (primary && !QApplication::clipboard()->supportsSelection()) return;
    QString t = QApplication::clipboard()->text(mode);
    if (t.isEmpty()) return;
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

void TerminalWidget::applyFontSize(int pt) {
    if (pt < 5)  pt = 5;
    if (pt > 64) pt = 64;
    if (pt == m_currentFontSize && m_renderer) return;
    m_currentFontSize = pt;

    QFont font = this->font();
    font.setPointSize(pt);
    setFont(font);

    QFontMetrics fm(font);
    m_cellWidth  = std::max(1, fm.horizontalAdvance(QChar('M')));
    m_cellHeight = std::max(1, fm.height());

    delete m_renderer;
    m_renderer = new renderer::QtRenderer(font, m_cellWidth, m_cellHeight);
    m_renderer->loadTheme(m_config.themePath());
    m_renderer->setCursorStyle(m_config.cursorStyle());
    if (m_config.foreground())  m_renderer->setDefaultFg(QColor::fromRgba(m_config.foreground()));
    if (m_config.background())  m_renderer->setDefaultBg(QColor::fromRgba(m_config.background()));
    if (m_config.cursorColor()) m_renderer->setCursorColor(QColor::fromRgba(m_config.cursorColor()));
    if (m_config.selectionBg() || m_config.selectionFg()) {
        m_renderer->setSelectionColors(
            m_config.selectionBg() ? QColor::fromRgba(m_config.selectionBg()) : QColor(0x44,0x44,0x66),
            m_config.selectionFg() ? QColor::fromRgba(m_config.selectionFg()) : QColor(0xFF,0xFF,0xFF));
    }
    m_renderer->setPadding(m_config.paddingX(), m_config.paddingY());

    // Re-grid based on the new cell metrics.
    resizeEvent(nullptr);
    m_terminal.setCellPixels(m_cellWidth, m_cellHeight);
    update();
}

// ---------------------------------------------------------------------------
// Find-in-scrollback
// ---------------------------------------------------------------------------
void TerminalWidget::positionFindBar() {
    if (!m_findEdit) return;
    QWidget* bar = m_findEdit->parentWidget();   // the frame, see openFindBar
    if (!bar) return;
    int barW = std::min(420, width() - 20);
    int barH = bar->sizeHint().height();
    bar->setGeometry(width() - barW - 10, 10, barW, barH);
    bar->raise();
}

void TerminalWidget::openFindBar() {
    if (m_findEdit) {
        m_findEdit->parentWidget()->show();
        m_findEdit->setFocus();
        m_findEdit->selectAll();
        positionFindBar();
        return;
    }
    // Frame so we get a solid background over the cell grid.
    auto* frame = new QFrame(this);
    frame->setFrameShape(QFrame::Panel);
    frame->setStyleSheet(
        "QFrame { background: #1e1e1e; border: 1px solid #4a4a4a; }"
        "QLineEdit { background: #2a2a2a; color: #f0f0f0; border: 0; }"
        "QLabel { color: #b0b0b0; }");
    auto* lay = new QHBoxLayout(frame);
    lay->setContentsMargins(8, 4, 8, 4);
    lay->setSpacing(8);

    auto* label = new QLabel("Find:", frame);
    m_findEdit  = new QLineEdit(frame);
    m_findEdit->setPlaceholderText("type to search scrollback…");
    m_findCount = new QLabel("", frame);
    m_findCount->setMinimumWidth(60);

    lay->addWidget(label);
    lay->addWidget(m_findEdit, 1);
    lay->addWidget(m_findCount);

    connect(m_findEdit, &QLineEdit::textChanged, this, [this](const QString& s) {
        if (s.isEmpty()) { m_hasSelection = false; m_findCount->setText(""); update(); return; }
        findNext();
    });
    connect(m_findEdit, &QLineEdit::returnPressed, this, [this]() {
        if (QApplication::keyboardModifiers() & Qt::ShiftModifier) findPrev();
        else findNext();
    });

    // Esc closes — install as event filter rather than reimplementing
    // keyPressEvent on QLineEdit.
    m_findEdit->installEventFilter(this);

    positionFindBar();
    frame->show();
    m_findEdit->setFocus();
}

void TerminalWidget::closeFindBar() {
    if (!m_findEdit) return;
    m_findEdit->parentWidget()->hide();
    setFocus();
}

void TerminalWidget::findNext() {
    if (!m_findEdit) return;
    QString needle = m_findEdit->text();
    if (needle.isEmpty()) return;

    long long startAbs;
    int startCol;
    if (m_hasSelection) {
        // Resume after the current match's end.
        SelPoint a = m_selAnchor, b = m_selFocus;
        if (a.absRow > b.absRow || (a.absRow == b.absRow && a.col > b.col)) std::swap(a, b);
        startAbs = b.absRow;
        startCol = b.col + 1;
    } else {
        startAbs = (long long)m_terminal.grid().absScroll() - m_viewportOffset;
        startCol = 0;
    }

    long long hitRow; int hitStart, hitEnd;
    if (findFromAbs(startAbs, startCol, +1, needle, hitRow, hitStart, hitEnd)) {
        m_selAnchor = { hitRow, hitStart };
        m_selFocus  = { hitRow, hitEnd };
        m_hasSelection = true;
        scrollIntoView(hitRow);
        m_findCount->setText("match");
        update();
    } else {
        m_findCount->setText("no match");
    }
}

void TerminalWidget::findPrev() {
    if (!m_findEdit) return;
    QString needle = m_findEdit->text();
    if (needle.isEmpty()) return;

    long long startAbs;
    int startCol;
    if (m_hasSelection) {
        SelPoint a = m_selAnchor, b = m_selFocus;
        if (a.absRow > b.absRow || (a.absRow == b.absRow && a.col > b.col)) std::swap(a, b);
        startAbs = a.absRow;
        startCol = a.col - 1;
    } else {
        startAbs = (long long)m_terminal.grid().absScroll();
        startCol = m_terminal.grid().cols() - 1;
    }

    long long hitRow; int hitStart, hitEnd;
    if (findFromAbs(startAbs, startCol, -1, needle, hitRow, hitStart, hitEnd)) {
        m_selAnchor = { hitRow, hitStart };
        m_selFocus  = { hitRow, hitEnd };
        m_hasSelection = true;
        scrollIntoView(hitRow);
        m_findCount->setText("match");
        update();
    } else {
        m_findCount->setText("no match");
    }
}

// Single-row substring search across history + live grid. Returns the
// first match at-or-beyond (fromAbsRow, fromCol) when dir == +1, or
// at-or-before when dir == -1. Match coordinates are absolute.
bool TerminalWidget::findFromAbs(long long fromAbsRow, int fromCol, int dir,
                                 const QString& needle,
                                 long long& outAbsRow, int& outStartCol, int& outEndCol) const {
    const auto& grid = m_terminal.grid();
    int histRows = grid.historyLines();
    int liveRows = grid.rowCount();
    long long topAbs = grid.absScroll() - histRows;
    long long botAbs = grid.absScroll() + liveRows - 1;

    auto rowAt = [&](long long abs) -> const std::vector<renderer::Cell>* {
        long long idx = abs - topAbs;
        if (idx < 0 || idx >= histRows + liveRows) return nullptr;
        if (idx < histRows) return &grid.historyRow((int)idx);
        return &grid.rows()[(int)(idx - histRows)];
    };
    auto rowText = [&](long long abs) -> QString {
        const auto* row = rowAt(abs);
        if (!row) return {};
        QString s;
        for (const auto& c : *row) {
            uint32_t cp = c.ch ? c.ch : ' ';
            s += QString::fromUcs4(reinterpret_cast<const char32_t*>(&cp), 1);
        }
        return s;
    };

    long long r = std::clamp<long long>(fromAbsRow, topAbs, botAbs);
    int n = needle.length();
    Qt::CaseSensitivity cs = Qt::CaseInsensitive;
    while (r >= topAbs && r <= botAbs) {
        QString rt = rowText(r);
        if (dir > 0) {
            int hit = rt.indexOf(needle, (r == fromAbsRow ? fromCol : 0), cs);
            if (hit >= 0) {
                outAbsRow   = r;
                outStartCol = hit;
                outEndCol   = hit + n - 1;
                return true;
            }
            ++r;
        } else {
            int end = (r == fromAbsRow ? fromCol + n : rt.length());
            int hit = rt.lastIndexOf(needle, end, cs);
            if (hit >= 0) {
                outAbsRow   = r;
                outStartCol = hit;
                outEndCol   = hit + n - 1;
                return true;
            }
            --r;
        }
    }
    return false;
}

void TerminalWidget::scrollIntoView(long long absRow) {
    long long topVisible = (long long)m_terminal.grid().absScroll() - m_viewportOffset;
    long long botVisible = topVisible + m_terminal.grid().rowCount() - 1;
    if (absRow >= topVisible && absRow <= botVisible) return;

    long long desiredTop;
    if (absRow < topVisible) {
        // Scroll back so the match sits ~1/3 down the screen.
        desiredTop = absRow - m_terminal.grid().rowCount() / 3;
    } else {
        desiredTop = absRow - 2 * m_terminal.grid().rowCount() / 3;
    }
    long long offset = (long long)m_terminal.grid().absScroll() - desiredTop;
    if (offset < 0) offset = 0;
    if (offset > m_terminal.grid().historyLines()) offset = m_terminal.grid().historyLines();
    m_viewportOffset = (int)offset;
}

bool TerminalWidget::eventFilter(QObject* obj, QEvent* ev) {
    if (m_findEdit && obj == m_findEdit && ev->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(ev);
        if (ke->key() == Qt::Key_Escape) {
            closeFindBar();
            return true;
        }
    }
    return QWidget::eventFilter(obj, ev);
}

} // namespace brain::ui
