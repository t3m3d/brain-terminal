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
}

// ------------------------------------------------------------
// Renderer setup -- uses config.font + theme
// ------------------------------------------------------------
void TerminalWidget::setupRenderer() {
    // Load font from config; force a fixed-pitch (monospace) face so the cell
    // grid lines up.
    QFont font(QString::fromStdString(m_config.fontFamily()),
               m_config.fontSize());
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    setFont(font);

    // Derive the cell size from the ACTUAL font metrics. (Was a hardcoded
    // 8x16 that didn't match the font -> overlapping rows / misaligned cols.)
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