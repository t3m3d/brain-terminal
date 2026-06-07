#include "brain/ui/TerminalWindow.hpp"
#include "brain/ui/TerminalWidget.hpp"

namespace brain::ui {

TerminalWindow::TerminalWindow(const brain::Config& config)
    : m_config(config)
{
    setWindowTitle("brain");

    // Top-level translucency must be set on the window for the central
    // widget's transparent background to reach the compositor (Hyprland etc.).
    if (config.opacityPercent() < 100)
        setAttribute(Qt::WA_TranslucentBackground);

    // Pass config into TerminalWidget
    setCentralWidget(new TerminalWidget(m_config, this));

    resize(config.windowWidth(), config.windowHeight());
}

}