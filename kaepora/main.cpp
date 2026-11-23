// Kaepora: makes seeds, keeps them, and puts the one in use into the emulator's mods folder.
#include "game_values.h"
#include "window.h"

#include <QApplication>

#include <clocale>
#include <cstdlib>

int main(int argc, char** argv) {
#ifdef _WIN32
    // A failed assert stops the window, as on Linux. Without a console, the C runtime would ask in a box, whose Ignore
    // button carries on past it.
    _set_error_mode(_OUT_TO_STDERR);
#endif
    QApplication app(argc, argv);
    // Qt takes the user's locale when the application starts. The generator handles text as the C locale does, the
    // locale its command-line tools run in.
    std::setlocale(LC_ALL, "C");
    QApplication::setOrganizationName("kaepora");
    QApplication::setApplicationName("kaepora");
    // Names the desktop file, so the desktop shows the window with its icon.
    QApplication::setDesktopFileName("org.kaepora.Kaepora");
    Window window(DebugServerPort);
    window.show();
    return app.exec();
}
