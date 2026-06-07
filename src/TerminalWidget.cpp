#include "brain/ui/TerminalWidget.hpp"
#include <QPainter>
#include <QKeyEvent>
#include <QFont>
#include <QFontMetrics>

namespace brain::ui {

TerminalWidget::TerminalWidget(const brain::Config& config, QWidget* parent)
    : QWidget(parent),
      m_config(config),
      m_terminal(80, 24),   // initial size; updated on resizeEvent
      m_input(),
      m_pty()
{
    setFocusPolicy(Qt::StrongFocus);

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
}

// ------------------------------------------------------------
// PTY setup -- uses config.shell()
// ------------------------------------------------------------
void TerminalWidget::setupPTY() {
    // PTY -> Terminal
    m_pty.setOutputCallback([this](const std::vector<char>& data) {
        m_terminal.onPTYOutput(data);
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
    // Build the font from config, but VERIFY Qt actually picked a fixed-
    // pitch face. Qt's font matcher silently falls back to the system
    // proportional font when the named family is absent (eg "Monospace"
    // doesn't exist on stock Windows -> falls back to Microsoft Sans
    // Serif, where 'M' is much wider than 'i'). The cell grid then uses
    // M's width and we get the "M i c r o s o f t" widely-spaced look.
    QFont font(QString::fromStdString(m_config.fontFamily()),
               m_config.fontSize());
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
}

// ------------------------------------------------------------
// Paint event -- draw terminal grid
// ------------------------------------------------------------
void TerminalWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);

    if (m_renderer) {
        m_renderer->render(painter, m_terminal.grid());
    }
}

// ------------------------------------------------------------
// Keyboard -> PTY
// ------------------------------------------------------------
void TerminalWidget::keyPressEvent(QKeyEvent* e) {
    using namespace brain::input;
    Modifier mod = Modifier::None;
    if (e->modifiers() & Qt::ShiftModifier) mod = Modifier::Shift;
    else if (e->modifiers() & Qt::ControlModifier) mod = Modifier::Ctrl;
    else if (e->modifiers() & Qt::AltModifier) mod = Modifier::Alt;

    std::string seq = m_input.translateToEscape(e->key(), mod);
    if (!seq.empty()) {
        m_pty.writeInput(seq);
    }
}

// ------------------------------------------------------------
// Resize terminal + renderer
// ------------------------------------------------------------
void TerminalWidget::resizeEvent(QResizeEvent*) {
    int cols = width() / m_cellWidth;
    int rows = height() / m_cellHeight;

    m_terminal.resize(cols, rows);

    if (m_renderer) {
        m_renderer->resize(cols, rows);
    }

    m_pty.resize(cols, rows);
}

} // namespace brain::ui