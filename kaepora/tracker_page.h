#pragma once

#include <QObject>
#include <QStringList>
#include <QTcpServer>

#include <cstddef>
#include <vector>

class QImage;
class QTcpSocket;

constexpr quint16 TrackerPagePort = 21199;

// Serves the tracker's picture to OBS's Browser source: a page that shows the picture and swaps in each new one.
class TrackerPage : public QObject {
public:
    // A connection that sends this much without ending its request is closed.
    static constexpr qint64 RequestLimit = 8 * 1024;
    // How many connections of each kind stay open: those that ask for the page or the picture, and those that listen
    // for changes. A new one past this closes the oldest of its kind.
    static constexpr std::size_t ConnectionLimit = 16;

    explicit TrackerPage(quint16 port, QObject* parent = nullptr);
    // Listens from now on on this computer only, or to the local network as well, and closes the connections of an
    // earlier call. False when the port is taken.
    bool listen(bool network);
    void show(const QImage& picture);
    // What a streamer pastes into OBS: the address for OBS on this computer, and, while it listens to the network,
    // the addresses another computer can use, by each of this computer's network addresses and then by its name. None
    // while it does not listen.
    QStringList links() const;

private:
    void take();
    void read(QTcpSocket* connection);

    const quint16 port;
    QByteArray png;
    QByteArray name;
    bool network = false;
    std::vector<QTcpSocket*> requests;
    std::vector<QTcpSocket*> listeners;
    QTcpServer server;
};
