#include "brain/ui/TerminalWindow.hpp"
#include "brain/ui/TerminalWidget.hpp"
#include <QApplication>
#include <QTabWidget>
#include <QTabBar>
#include <QShortcut>
#include <QKeySequence>
#include <QCloseEvent>

namespace brain::ui {

TerminalWindow::TerminalWindow(const brain::Config& config)
    : m_config(config)
{
    setWindowTitle("brain");

    // Top-level translucency must be set on the window for the central
    // widget's transparent background to reach the compositor (Hyprland,
    // DWM…). Skip when opacity == 100 — opaque windows are the fast path.
    if (config.opacityPercent() < 100)
        setAttribute(Qt::WA_TranslucentBackground);

    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->tabBar()->setAutoHide(true);
    setCentralWidget(m_tabs);

    connect(m_tabs, &QTabWidget::tabCloseRequested,
            this,   &TerminalWindow::onTabCloseRequested);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) {
        updateWindowTitleFromActive();
    });

    newTab();

    auto bind = [this](const QKeySequence& seq, void (TerminalWindow::*slot)()) {
        auto* sc = new QShortcut(seq, this);
        sc->setContext(Qt::ApplicationShortcut);
        connect(sc, &QShortcut::activated, this, slot);
    };
    bind(QKeySequence("Ctrl+Shift+T"),   &TerminalWindow::newTab);
    bind(QKeySequence("Ctrl+Shift+W"),   &TerminalWindow::closeCurrentTab);
    bind(QKeySequence("Ctrl+Tab"),       &TerminalWindow::nextTab);
    bind(QKeySequence("Ctrl+Shift+Tab"), &TerminalWindow::prevTab);
    bind(QKeySequence("Ctrl+PgDown"),    &TerminalWindow::nextTab);
    bind(QKeySequence("Ctrl+PgUp"),      &TerminalWindow::prevTab);

    int w = config.windowWidth()  > 0 ? config.windowWidth()  : 1000;
    int h = config.windowHeight() > 0 ? config.windowHeight() : 640;
    resize(w, h);
}

void TerminalWindow::closeEvent(QCloseEvent* e) {
    // Allow the OS close button to terminate every tab's PTY cleanly via
    // QObject destruction; nothing extra to do here.
    e->accept();
}

TerminalWidget* TerminalWindow::currentTerm() const {
    return qobject_cast<TerminalWidget*>(m_tabs->currentWidget());
}

void TerminalWindow::hookTabSignals(TerminalWidget* w, int tabIndex) {
    connect(w, &TerminalWidget::titleChanged, this, [this, w](const QString& t) {
        int idx = m_tabs->indexOf(w);
        if (idx >= 0) {
            QString label = t.isEmpty() ? QString("brain") : t;
            // Trim very long titles in the tab strip; the full title still
            // shows in the window title and the tab tooltip.
            const int maxLen = 24;
            QString shown = (label.length() > maxLen)
                          ? label.left(maxLen - 1) + QChar(0x2026)   // …
                          : label;
            m_tabs->setTabText(idx, shown);
            m_tabs->setTabToolTip(idx, label);
        }
        if (m_tabs->currentWidget() == w) updateWindowTitleFromActive();
    });
    connect(w, &TerminalWidget::bellRang, this, [this, w]() {
        // Alert the window only if the bell came from the active tab.
        if (m_tabs->currentWidget() == w) {
            QApplication::alert(this, 400);
        } else {
            // For background tabs, mark the tab strip so the user notices.
            int idx = m_tabs->indexOf(w);
            if (idx >= 0) {
                QString cur = m_tabs->tabText(idx);
                if (!cur.startsWith(QChar(0x2022)))   // bullet •
                    m_tabs->setTabText(idx, QChar(0x2022) + cur);
            }
        }
    });
    (void)tabIndex;
}

void TerminalWindow::updateWindowTitleFromActive() {
    auto* tw = currentTerm();
    if (!tw) { setWindowTitle("brain"); return; }
    int idx = m_tabs->indexOf(tw);
    QString full = m_tabs->tabToolTip(idx);
    if (full.isEmpty()) full = "brain";
    setWindowTitle(full == "brain" ? full : (full + " — brain"));
}

void TerminalWindow::newTab() {
    auto* tw = new TerminalWidget(m_config, m_tabs);
    int idx = m_tabs->addTab(tw, "brain");
    m_tabs->setCurrentIndex(idx);
    tw->setFocus();
    hookTabSignals(tw, idx);
}

void TerminalWindow::closeCurrentTab() {
    int idx = m_tabs->currentIndex();
    if (idx < 0) return;
    onTabCloseRequested(idx);
}

void TerminalWindow::onTabCloseRequested(int index) {
    QWidget* w = m_tabs->widget(index);
    if (!w) return;
    m_tabs->removeTab(index);
    w->deleteLater();
    if (m_tabs->count() == 0) close();
}

void TerminalWindow::nextTab() {
    int n = m_tabs->count();
    if (n < 2) return;
    m_tabs->setCurrentIndex((m_tabs->currentIndex() + 1) % n);
    if (auto* tw = currentTerm()) tw->setFocus();
}

void TerminalWindow::prevTab() {
    int n = m_tabs->count();
    if (n < 2) return;
    m_tabs->setCurrentIndex((m_tabs->currentIndex() - 1 + n) % n);
    if (auto* tw = currentTerm()) tw->setFocus();
}

void TerminalWindow::onTabTitleChanged(const QString&) {}
void TerminalWindow::onTabBellRang() {}

}
