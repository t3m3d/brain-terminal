#include "brain/ui/TerminalWindow.hpp"
#include "brain/ui/TerminalWidget.hpp"
#include <QApplication>
#include <QTabWidget>
#include <QTabBar>
#include <QShortcut>
#include <QKeySequence>
#include <QCloseEvent>
#include <cstdlib>
#include <cctype>
#include <string>

namespace brain::ui {

namespace {

bool envSet(const char* k) { const char* v = std::getenv(k); return v && *v; }

// True under a tiling WM/compositor (where you split with the WM, not tabs).
bool detectTilingWM() {
    if (envSet("HYPRLAND_INSTANCE_SIGNATURE")) return true;
    if (envSet("SWAYSOCK"))                    return true;
    if (envSet("I3SOCK"))                      return true;
    if (envSet("NIRI_SOCKET"))                 return true;

    static const char* const tiling[] = {
        "hyprland","sway","i3","river","niri","dwl","dwm","bspwm","awesome",
        "xmonad","qtile","herbstluftwm","spectrwm","leftwm","wmii","ratpoison",
        "cwm","notion","stumpwm","scrollwm","hikari","cagebreak","dk", nullptr };
    for (const char* key : {"XDG_CURRENT_DESKTOP","XDG_SESSION_DESKTOP","DESKTOP_SESSION"}) {
        const char* raw = std::getenv(key);
        if (!raw || !*raw) continue;
        std::string s(raw);
        for (char& ch : s) ch = static_cast<char>(std::tolower((unsigned char)ch));
        size_t start = 0;
        while (start <= s.size()) {
            size_t colon = s.find(':', start);
            std::string tok = s.substr(start, colon == std::string::npos ? colon : colon - start);
            for (const char* const* t = tiling; *t; ++t)
                if (tok == *t) return true;
            if (colon == std::string::npos) break;
            start = colon + 1;
        }
    }
    return false;
}

// tabs = auto|on|off. auto → enabled unless a tiling WM is detected.
bool resolveTabsEnabled(const brain::Config& cfg) {
    const std::string& m = cfg.tabsMode();
    if (m == "on"  || m == "true"  || m == "yes" || m == "always" || m == "enabled")  return true;
    if (m == "off" || m == "false" || m == "no"  || m == "never"  || m == "disabled") return false;
    return !detectTilingWM();
}

} // namespace

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

    // Tabs on for stacking desktops, off for tiling WMs; when off the tab
    // keybindings aren't registered and the lone tab's bar stays auto-hidden.
    if (resolveTabsEnabled(config)) {
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
    }

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
        const std::string& mode = m_config.bell();   // urgent | audible | none
        if (mode == "none") return;
        // Alert the window only if the bell came from the active tab.
        if (m_tabs->currentWidget() == w) {
            if (mode == "audible") QApplication::beep();
            else                   QApplication::alert(this, 400);   // "urgent" (default)
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
