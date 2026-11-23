#pragma once

#include <QMessageBox>

// Message boxes open window-modal and return at once, so the window's events go on while one is open.
inline void Tell(QWidget* window, QMessageBox::Icon icon, const QString& text) {
    auto* box = new QMessageBox(icon, "Kaepora", text, QMessageBox::Ok, window);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
}
