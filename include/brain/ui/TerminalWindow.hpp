#pragma once
#include <QMainWindow>
#include "TerminalWidget.hpp"
#include "brain/Config.hpp"

class QTabWidget;
class QShortcut;

namespace brain::ui {

class TerminalWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit TerminalWindow(const brain::Config& config);

protected:
    void closeEvent(QCloseEvent*) override;

private slots:
    void newTab();
    void closeCurrentTab();
    void nextTab();
    void prevTab();
    void onTabTitleChanged(const QString& title);
    void onTabBellRang();
    void onTabCloseRequested(int index);

private:
    brain::Config m_config;
    QTabWidget*   m_tabs = nullptr;

    TerminalWidget* currentTerm() const;
    void hookTabSignals(TerminalWidget* w, int tabIndex);
    void updateWindowTitleFromActive();
};

}
