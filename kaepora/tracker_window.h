#pragma once

#include "game_watch.h"
#include "tracker.h"
#include "tracker_painter.h"

#include <QWidget>

class QMenu;

// The tracker's grid in a window of its own, made to be captured on stream: it holds the grid and nothing else.
class TrackerWindow : public QWidget {
    Q_OBJECT

public:
    // Follows the game through Lichii's debug server on this port, and draws these pictures by their names.
    TrackerWindow(quint16 port, QHash<QString, QImage> pictures);

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
    void chooseBackground();
    void setStatus(const QString& status);

    const QHash<QString, QImage> pictures;
    Tracker tracker;
    std::vector<CellView> cells;
    TrackerLook look;
    QString lastStatus;
    QMenu* menu;
    GameWatch watch;
};
