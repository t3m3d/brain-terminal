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
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    brain::Config m_config;          // NEW: store config
    core::Terminal m_terminal;
    renderer::QtRenderer* m_renderer = nullptr;
    input::InputHandler m_input;
    pty::PTY m_pty;

    int m_cellWidth = 8;
    int m_cellHeight = 16;
    int m_fontSize = 14;             // live font size (Ctrl +/-/0 adjusts it)

    void setupPTY();                 // will use config.shell()
    void setupRenderer();            // builds font + renderer at m_fontSize
    void recomputeGrid();            // cols/rows from geometry; resize terminal/pty
    void applyFontSize(int size);    // rebuild renderer at a new size

    // Selection + clipboard.
    void copySelectionToClipboard();
    void pasteFromClipboard(bool primary);
    int  rowAtY(int y) const;
    int  colAtX(int x) const;
    bool m_selecting = false;
    bool m_hasSelection = false;
    int  m_selStartRow = 0, m_selStartCol = 0;
    int  m_selEndRow = 0, m_selEndCol = 0;

    // Scrollback view offset (lines scrolled up from the live bottom; 0 = live).
    int  m_scrollOffset = 0;
};

}