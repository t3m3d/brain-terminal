#include <QApplication>
#include <QDir>
#include <QStandardPaths>

#include "brain/Config.hpp"
#include "brain/ui/TerminalWindow.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);

    brain::Config config = brain::Config::load("");

    brain::ui::TerminalWindow win(config);

    // Window opacity. 100 % is fully opaque (default). Below that, Qt asks
    // the compositor to alpha-blend the entire window; on Windows this
    // works under DWM out of the box. Values <40 % are reading-difficult
    // against most desktops; we still respect them, the user gets to
    // pick. 0 means "use the default" (opaque) — guards against an
    // accidentally-zero conf entry making the window invisible.
    int op = config.opacityPercent();
    if (op > 0 && op < 100) {
        qreal v = op / 100.0;
        if (v < 0.1) v = 0.1;        // floor, see note above
        win.setWindowOpacity(v);
    }

    win.show();
    return app.exec();
}