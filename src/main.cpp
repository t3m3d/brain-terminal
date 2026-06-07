#include <QApplication>
#include <QDir>
#include <QStandardPaths>

#include "kterm/Config.hpp"
#include "kterm/ui/TerminalWindow.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);


    // ------------------------------------------------------------
    // Step 7: Ensure user config directory exists
    // ------------------------------------------------------------

    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);   // creates directory if missing

    // ------------------------------------------------------------
    // Load config (search order: user -> local -> built-in)
    // ------------------------------------------------------------

    kterm::Config config = kterm::Config::load("");

    // ------------------------------------------------------------
    // Create the main window and pass config to it
    // ------------------------------------------------------------
    kterm::ui::TerminalWindow win(config);
    win.show();

    return app.exec();
}