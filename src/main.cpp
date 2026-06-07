#include <QApplication>
#include <QDir>
#include <QStandardPaths>

#include "brain/Config.hpp"
#include "brain/ui/TerminalWindow.hpp"

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

    brain::Config config = brain::Config::load("");

    // ------------------------------------------------------------
    // Create the main window and pass config to it
    // ------------------------------------------------------------
    brain::ui::TerminalWindow win(config);
    win.show();

    return app.exec();
}