#pragma once

#include "game_watch.h"
#include "tracker.h"
#include "tracker_page.h"
#include "tracker_painter.h"

#include <QWidget>

class QMenu;

// The tracker's grid in a window of its own, made to be captured on stream: it holds the grid and nothing else. While
// it is open, its page shows the same grid to OBS's Browser source.
class TrackerWindow : public QWidget {
    Q_OBJECT

public:
    // Follows the game through Lichii's debug server on this port, serves its page on pagePort, and draws these
    // pictures by their names.
    TrackerWindow(quint16 port, quint16 pagePort, QHash<QString, QImage> pictures);

signals:
    // What the tracker is doing, in a sentence, each time it changes.
    void statusChanged(const QString& status);

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void lookChanged();
    void showOnPage();
    void chooseBackground();
    void openBrowserSource();
    void setStatus(const QString& status);

    const QHash<QString, QImage> pictures;
    Tracker tracker;
    std::vector<CellView> cells;
    TrackerLook look;
    int backgroundStrength;
    bool network;
    QString lastStatus;
    QMenu* menu;
    const quint16 pagePort;
    TrackerPage page;
    GameWatch watch;
};
