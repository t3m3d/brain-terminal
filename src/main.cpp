#include <QApplication>
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QStandardPaths>

#include "brain/Config.hpp"
#include "brain/ui/TerminalWindow.hpp"

// Locate the app icon. The icon is compiled into the binary via the .qrc
// (resources/brain.qrc), so ":/icons/brain.png" always resolves no matter
// where brain was launched from — that's the reliable source. The on-disk
// candidates are a fallback for an installed icon theme / dev tree.
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

    // On Wayland (Hyprland/sway) the compositor and bars/launchers can't take a
    // per-window QIcon directly — they map the window's app_id to a .desktop
    // entry and read Icon= from it. Setting the desktop file name makes Qt
    // advertise app_id "brain", matching resources/brain.desktop (installed to
    // share/applications) → the brain icon shows in the bar / alt-tab / dock.
    // On X11 this is harmless and the QIcon below drives the icon directly.
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
