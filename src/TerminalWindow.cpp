#include "kterm/ui/TerminalWindow.hpp"
#include "kterm/ui/TerminalWidget.hpp"

namespace kterm::ui {

TerminalWindow::TerminalWindow(const kterm::Config& config)
    : m_config(config)
{
    setWindowTitle("brain");

    // Pass config into TerminalWidget
    setCentralWidget(new TerminalWidget(m_config, this));

    resize(800, 600);
}

}