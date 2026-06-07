#include "brain/ui/TerminalWindow.hpp"
#include "brain/ui/TerminalWidget.hpp"

namespace brain::ui {

TerminalWindow::TerminalWindow(const brain::Config& config)
    : m_config(config)
{
    setWindowTitle("brain");

    // Pass config into TerminalWidget
    setCentralWidget(new TerminalWidget(m_config, this));

    resize(800, 600);
}

}