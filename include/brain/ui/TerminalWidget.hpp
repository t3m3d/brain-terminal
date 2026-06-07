#pragma once
#include <QWidget>
#include <QTimer>

#include "brain/Config.hpp"
#include "brain/core/Terminal.hpp"
#include "brain/renderer/QtRenderer.hpp"
#include "brain/input/InputHandler.hpp"
#include "brain/pty/PTY.hpp"

namespace brain::ui {

class TerminalWidget : public QWidget {
    Q_OBJECT

public:
    // NEW: constructor now accepts Config
    TerminalWidget(const brain::Config& config, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    brain::Config m_config;          // NEW: store config
    core::Terminal m_terminal;
    renderer::QtRenderer* m_renderer = nullptr;
    input::InputHandler m_input;
    pty::PTY m_pty;

    int m_cellWidth = 8;
    int m_cellHeight = 16;

    void setupPTY();                 // will use config.shell()
    void setupRenderer();            // will use config.fontFamily(), fontSize(), themePath()
};

}