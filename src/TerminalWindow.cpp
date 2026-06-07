#include "brain/ui/TerminalWindow.hpp"
#include "brain/ui/TerminalWidget.hpp"
#include <QApplication>
#include <QStyle>
#include <QTimer>

namespace brain::ui {

TerminalWindow::TerminalWindow(const brain::Config& config)
    : m_config(config)
{
    setWindowTitle("brain");

    auto* tw = new TerminalWidget(m_config, this);
    setCentralWidget(tw);

    // OSC 0/2 → window title. Shells with $PS1-driven titles, vim, less,
    // tmux all do this — matches every other terminal.
    connect(tw, &TerminalWidget::titleChanged, this, [this](const QString& t) {
        if (t.isEmpty()) setWindowTitle("brain");
        else             setWindowTitle(t + " — brain");
    });

    // BEL — flash the window briefly. Cheap, no audio dep, matches
    // ghostty/iTerm "visual bell" mode.
    connect(tw, &TerminalWidget::bellRang, this, [this]() {
        QApplication::alert(this, 400);
    });

    int w = m_config.windowWidth()  > 0 ? m_config.windowWidth()  : 1000;
    int h = m_config.windowHeight() > 0 ? m_config.windowHeight() : 640;
    resize(w, h);
}

}
