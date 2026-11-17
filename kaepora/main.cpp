// Kaepora: makes seeds, keeps them, and puts the one in use into the emulator's mods folder.
#include "window.h"

#include <QApplication>

#include <clocale>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // Qt takes the user's locale when the application starts. The generator handles text as the C locale does, the
    // locale its command-line tools run in.
    std::setlocale(LC_ALL, "C");
    QApplication::setOrganizationName("kaepora");
    QApplication::setApplicationName("kaepora");
    // Names the desktop file, so the desktop shows the window with its icon.
    QApplication::setDesktopFileName("io.github.pixelhabits.kaepora");
    Window window;
    window.show();
    return app.exec();
}
