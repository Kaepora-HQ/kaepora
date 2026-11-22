#include "tracker_page.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QHostInfo>
#include <QImage>
#include <QNetworkInterface>
#include <QNetworkProxy>
#include <QTcpSocket>

#include <algorithm>

constexpr char Page[] = R"(<!doctype html>
<meta charset="utf-8">
<title>Kaepora tracker</title>
<style>
html, body { height: 100%; margin: 0; overflow: hidden; }
img { display: block; width: 100%; height: 100%; object-fit: contain; }
</style>
<img alt="">
<script>
let shown = document.querySelector("img");
let wanted;
// A picture is refused once another is shown. An image asked for while an earlier request for its address is on its
// way shares that request's refusal, so a refused picture still wanted is asked for once more.
const ask = (name, again) => {
  const next = wanted = new Image();
  next.src = "/picture.png?" + name;
  next.decode().then(() => {
    if (next === wanted) {
      shown.replaceWith(next);
      shown = next;
    }
  }, () => {
    if (next === wanted && again) {
      ask(name, false);
    }
  });
};
new EventSource("/changes").onmessage = ({ data }) => ask(data, true);
</script>
)";

static QByteArray Head(const QByteArray& type) {
    return "HTTP/1.1 200 OK\r\nContent-Type: " + type + "\r\nCache-Control: no-store\r\n";
}

static QByteArray Event(const QByteArray& name) {
    return "data: " + name + "\n\n";
}

static void Reply(QTcpSocket* connection, const QByteArray& type, const QByteArray& body) {
    connection->write(Head(type) + "Content-Length: " + QByteArray::number(body.size()) +
                      "\r\nConnection: close\r\n\r\n" + body);
    connection->disconnectFromHost();
}

TrackerPage::TrackerPage(quint16 port, QObject* parent) : QObject(parent), port(port) {
    // For any address but the loopback one, Qt would ask the system's proxy to listen in the page's place.
    server.setProxy(QNetworkProxy::NoProxy);
    connect(&server, &QTcpServer::newConnection, this, &TrackerPage::take);
}

bool TrackerPage::listen(bool network) {
    server.close();
    qDeleteAll(findChildren<QTcpSocket*>());
    this->network = network;
    return server.listen(network ? QHostAddress::Any : QHostAddress::LocalHost, port);
}

void TrackerPage::show(const QImage& picture) {
    png.clear();
    QBuffer buffer(&png);
    picture.save(&buffer, "PNG");
    // A page reuses a picture it already loaded from an address, whatever Cache-Control says (HTML's list of available
    // images), and it stays open while Kaepora restarts. Named by its content, an address never means two pictures.
    const QByteArray shown = QCryptographicHash::hash(png, QCryptographicHash::Sha1).toHex();
    if (shown == name) {
        return;
    }
    name = shown;
    for (QTcpSocket* listener : listeners) {
        listener->write(Event(name));
    }
}

QStringList TrackerPage::links() const {
    if (!server.isListening()) {
        return {};
    }
    const auto link = [this](const QString& host) {
        return QString("http://%1:%2/").arg(host).arg(server.serverPort());
    };
    QStringList links{ link("127.0.0.1") };
    if (network) {
        for (const QHostAddress& address : QNetworkInterface::allAddresses()) {
            if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback()) {
                links.append(link(address.toString()));
            }
        }
        links.append(link(QHostInfo::localHostName()));
    }
    return links;
}

void TrackerPage::take() {
    while (QTcpSocket* connection = server.nextPendingConnection()) {
        if (requests.size() == ConnectionLimit) {
            requests.front()->abort();
        }
        requests.push_back(connection);
        connection->setParent(this);
        connection->setReadBufferSize(RequestLimit);
        connect(connection, &QTcpSocket::readyRead, this, [this, connection] { read(connection); });
        connect(connection, &QTcpSocket::disconnected, this, [this, connection] {
            std::erase(requests, connection);
            std::erase(listeners, connection);
            connection->deleteLater();
        });
    }
}

void TrackerPage::read(QTcpSocket* connection) {
    const QByteArray request = connection->peek(RequestLimit);
    if (!request.contains("\r\n\r\n")) {
        if (request.size() == RequestLimit) {
            connection->abort();
        }
        return;
    }
    disconnect(connection, &QTcpSocket::readyRead, this, nullptr);
    const QList<QByteArray> line = request.first(request.indexOf("\r\n")).split(' ');
    const QByteArray target = line.size() == 3 && line[0] == "GET" ? line[1] : QByteArray();
    if (target == "/") {
        Reply(connection, "text/html; charset=utf-8", Page);
    } else if (target == "/picture.png?" + name) {
        Reply(connection, "image/png", png);
    } else if (target == "/changes") {
        std::erase(requests, connection);
        if (listeners.size() == ConnectionLimit) {
            listeners.front()->abort();
        }
        listeners.push_back(connection);
        connection->write(Head("text/event-stream") + "\r\n" + (name.isEmpty() ? QByteArray() : Event(name)));
    } else {
        connection->abort();
    }
}
