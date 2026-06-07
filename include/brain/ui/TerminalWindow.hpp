#pragma once
#include <QMainWindow>
#include "TerminalWidget.hpp"
#include "brain/Config.hpp"

namespace brain::ui {

class TerminalWindow : public QMainWindow {
public:
    explicit TerminalWindow(const brain::Config& config);

private:
    brain::Config m_config;
};

}