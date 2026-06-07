#pragma once
#include <QWidget>
#include <QTimer>
#include <QPoint>
#include <QString>

#include "brain/Config.hpp"
#include "brain/core/Terminal.hpp"
#include "brain/renderer/QtRenderer.hpp"
#include "brain/input/InputHandler.hpp"
#include "brain/pty/PTY.hpp"

class QMouseEvent;
class QWheelEvent;
class QFocusEvent;
class QLineEdit;
class QLabel;

namespace brain::ui {

class TerminalWidget : public QWidget {
    Q_OBJECT

public:
    TerminalWidget(const brain::Config& config, QWidget* parent = nullptr);

    // Window-title escape (OSC 0 / OSC 2) arrives via the Terminal core
    // and propagates out to the top-level window through this signal.
signals:
    void titleChanged(const QString& title);
    void bellRang();

protected:
    void paintEvent(QPaintEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void focusInEvent(QFocusEvent*) override;
    void focusOutEvent(QFocusEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;

private:
    brain::Config m_config;
    core::Terminal m_terminal;
    renderer::QtRenderer* m_renderer = nullptr;
    input::InputHandler m_input;
    pty::PTY m_pty;

    int m_cellWidth = 8;
    int m_cellHeight = 16;

    // Scrollback view offset. 0 = live tail at the bottom (default);
    // positive values scroll backward through Grid::history().
    int m_viewportOffset = 0;

    // Selection state. Indices are ABSOLUTE — they survive scroll/scrollback
    // because Grid keeps a monotonically-growing m_absScroll counter.
    struct SelPoint { long long absRow = -1; int col = -1; };
    SelPoint m_selAnchor;
    SelPoint m_selFocus;
    bool m_selecting = false;
    bool m_hasSelection = false;

    // Focus tracks whether the cursor should render filled (focused) or
    // outlined (unfocused) — small visual cue, matches every modern term.
    bool m_focused = false;

    void setupPTY();
    void setupRenderer();
    void hookTerminalSignals();

    void copySelectionToClipboard();
    void pasteFromClipboard();
    QString selectionText() const;
    SelPoint pixelToCell(const QPoint& p) const;

    // Find-in-scrollback. The bar is a QLineEdit child positioned at the
    // top-right of the widget; toggled with Ctrl+F, dismissed with Esc.
    void openFindBar();
    void closeFindBar();
    void findNext();
    void findPrev();
    bool findFromAbs(long long fromAbsRow, int fromCol,
                     int dir,
                     const QString& needle,
                     long long& outAbsRow, int& outStartCol, int& outEndCol) const;
    void scrollIntoView(long long absRow);
    void positionFindBar();

    QLineEdit* m_findEdit  = nullptr;
    QLabel*    m_findCount = nullptr;
};

}
