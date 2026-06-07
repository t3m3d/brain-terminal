#include <QApplication>
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QStandardPaths>

#include "brain/Config.hpp"
#include "brain/ui/TerminalWindow.hpp"

// Icon is compiled in via resources/brain.qrc, so ":/icons/brain.png" resolves
// from any CWD; the on-disk paths are an installed-theme / dev-tree fallback.
static QIcon loadAppIcon() {
    QIcon embedded(":/icons/brain.png");
    if (!embedded.isNull()) return embedded;

    QStringList candidates = {
        QCoreApplication::applicationDirPath() + "/resources/icons/brain.png",
        QCoreApplication::applicationDirPath() + "/../share/brain/icons/brain.png",
        QCoreApplication::applicationDirPath() + "/../share/icons/hicolor/256x256/apps/brain.png",
        "resources/icons/brain-256.png",
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

    // Wayland app_id → brain.desktop → Icon=. Needed for the icon to show in
    // bars/launchers on Wayland (a bare QIcon isn't enough); harmless on X11.
    QGuiApplication::setDesktopFileName("brain");

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
