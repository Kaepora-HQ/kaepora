#pragma once

#include "generator/seed_settings.h"

#include <QPointer>
#include <QWidget>

#include <functional>

class QFileInfo;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QThread;
class QTreeWidget;
class TrackerWindow;

class Window : public QWidget {
public:
    Window();
    ~Window() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    QLineEdit* addPathRow(QFormLayout* form, const QString& label, const QString& key,
                          bool (QFileInfo::*valid)() const, const std::function<QString()>& choose);
    void chooseSettings();
    void showSettings();
    void createSeed();
    void useSeed();
    void openSpoilerLog();
    void deleteSeed();
    void play();
    void openTracker();
    void refresh(QString select = {});
    void updateButtons();
    QString seedNamedBy(const char* fileName) const;

    const QString library; // holds a folder for each seed made
    QString inUse;         // the seed in the mods folder, as of the last refresh
    QThread* making = nullptr;
    QThread* readingPictures = nullptr;
    QPointer<TrackerWindow> tracker;
    SeedSettings newSeedSettings;

    QLineEdit* gameFile;
    QLineEdit* modsFolder;
    QLineEdit* emulator;
    QLineEdit* seedField;
    QPushButton* createButton;
    QLabel* settingsSummary;
    QTreeWidget* seeds;
    QPushButton* useButton;
    QPushButton* spoilerButton;
    QPushButton* deleteButton;
    QPushButton* playButton;
    QPushButton* trackerButton;
    QLabel* status;
    QLabel* trackerStatus;
};
