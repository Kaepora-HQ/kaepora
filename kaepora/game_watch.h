#pragma once

#include "game_values.h"

#include <QSemaphore>
#include <QThread>

#include <chrono>
#include <vector>

class GameWatch : public QThread {
    Q_OBJECT

public:
    static constexpr std::chrono::milliseconds ReadPause{ 250 };
    static constexpr std::chrono::milliseconds RetryPause{ 1000 };

    GameWatch(quint16 port, std::vector<SavedValue> values);
    // Stops the thread and waits for it.
    ~GameWatch() override;

signals:
    // The first read, the first after a failed one, and each read that differs from the last. Each value is as
    // GameValues::read gives it.
    void changed(const std::vector<quint32>& values);
    // Each failed read, with why in a sentence a player can read.
    void unreadable(const QString& why);

protected:
    void run() override;

private:
    const quint16 port;
    const std::vector<SavedValue> values;
    QSemaphore stop;
};
