// The player's window: the game file, the mods folder and the emulator, then the seeds made so far and the one in use.
#pragma once

#include <QWidget>

#include <functional>

class QFileInfo;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QThread;

class Window : public QWidget {
public:
    Window();
    ~Window() override;

private:
    QLineEdit* addPathRow(QFormLayout* form, const QString& label, const QString& key,
                          bool (QFileInfo::*valid)() const, const std::function<QString()>& choose);
    void createSeed();
    void useSeed();
    void openSpoilerLog();
    void deleteSeed();
    void play();
    void refresh(QString select = {});
    void updateButtons();
    QString seedNamedBy(const char* fileName) const;

    const QString library; // holds a folder for each seed made
    QString inUse;         // the seed in the mods folder, as of the last refresh
    QThread* making = nullptr;

    QLineEdit* gameFile;
    QLineEdit* modsFolder;
    QLineEdit* emulator;
    QLineEdit* seedField;
    QPushButton* createButton;
    QListWidget* seeds;
    QPushButton* useButton;
    QPushButton* spoilerButton;
    QPushButton* deleteButton;
    QPushButton* playButton;
    QLabel* status;
};
