#include <QApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QStandardPaths>

#include "brain/Config.hpp"
#include "brain/ui/TerminalWindow.hpp"

// Locate the app icon. Walk the candidate list in priority order: an
// installed icon next to the binary, the dev-tree resources/icons/, then
// install prefix. First hit wins. Returns an empty QIcon if none found —
// the .rc embedded icon still drives the .exe icon in Explorer, so
// having nothing here only means no in-window icon.
static QIcon loadAppIcon() {
    QStringList candidates = {
        QCoreApplication::applicationDirPath() + "/resources/icons/brain.ico",
        QCoreApplication::applicationDirPath() + "/resources/icons/brain.png",
        QCoreApplication::applicationDirPath() + "/../share/brain/icons/brain.png",
        QCoreApplication::applicationDirPath() + "/../resources/icons/brain.png",
        "resources/icons/brain.ico",
        "resources/icons/brain.png",
    };
    for (const QString& p : candidates) {
        if (QFile::exists(p)) return QIcon(p);
    }
    return {};
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("brain");
    QApplication::setApplicationDisplayName("brain");

    QIcon icon = loadAppIcon();
    if (!icon.isNull()) QApplication::setWindowIcon(icon);

    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);

    brain::Config config = brain::Config::load("");

    brain::ui::TerminalWindow win(config);
    if (!icon.isNull()) win.setWindowIcon(icon);

    int op = config.opacityPercent();
    if (op > 0 && op < 100) {
        qreal v = op / 100.0;
        if (v < 0.1) v = 0.1;
        win.setWindowOpacity(v);
    }

    win.show();
    return app.exec();
}
