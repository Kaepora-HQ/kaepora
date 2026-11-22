#include "game_watch.h"

#include <optional>

GameWatch::GameWatch(quint16 port, std::vector<SavedValue> values) : port(port), values(std::move(values)) {
}

GameWatch::~GameWatch() {
    stop.release();
    wait();
}

void GameWatch::run() {
    std::optional<std::vector<quint32>> last;
    do {
        try {
            GameValues game(port);
            do {
                std::vector<quint32> read = game.read(values);
                if (read != last) {
                    last = std::move(read);
                    emit changed(*last);
                }
            } while (!stop.try_acquire_for(ReadPause));
            return;
        } catch (const GameUnreadable& failed) {
            last.reset();
            emit unreadable(QString::fromUtf8(failed.what()));
        }
    } while (!stop.try_acquire_for(RetryPause));
}
