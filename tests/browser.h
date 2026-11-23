#pragma once

#include <QAbstractSocket>
#include <QByteArray>
#include <QByteArrayList>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTest>
#include <QUrl>

#include <algorithm>
#include <optional>

inline constexpr char Changes[] = "/changes";

inline QByteArray Picture(const QByteArray& name) {
    return "/picture.png?" + name;
}

inline QByteArray Get(const QByteArray& path) {
    return "GET " + path + " HTTP/1.1\r\nHost: kaepora\r\nAccept: */*\r\nUser-Agent: Mozilla/5.0\r\n\r\n";
}

inline void Open(QTcpSocket& socket, const QUrl& link, const QByteArray& request) {
    socket.connectToHost(link.host(), link.port());
    socket.write(request);
}

inline QByteArray Ask(const QUrl& link, const QByteArray& request) {
    QTcpSocket socket;
    Open(socket, link, request);
    if (!QTest::qWaitFor([&] { return socket.state() == QAbstractSocket::UnconnectedState; })) {
        qWarning("The connection did not end.");
    }
    return socket.readAll();
}

struct Answer {
    int code = 0;
    QByteArray type;
    QByteArray body;
};

inline Answer Read(const QByteArray& bytes) {
    const qsizetype end = bytes.indexOf("\r\n\r\n");
    if (end < 0) {
        return {};
    }
    const QList<QByteArray> lines = bytes.first(end).split('\n');
    Answer answer{ .code = lines.front().split(' ').value(1).toInt(), .body = bytes.sliced(end + 4) };
    for (const QByteArray& line : lines) {
        if (line.toLower().startsWith("content-type:")) {
            answer.type = line.sliced(line.indexOf(':') + 1).trimmed();
        }
    }
    return answer;
}

inline QByteArrayList Told(QTcpSocket& stream) {
    static const QRegularExpression data("^data: ?(.*)$", QRegularExpression::MultilineOption);
    QByteArrayList names;
    for (const QRegularExpressionMatch& event :
         data.globalMatch(QString::fromUtf8(Read(stream.peek(stream.bytesAvailable())).body))) {
        names.append(event.captured(1).toUtf8());
    }
    return names;
}

inline QByteArray Current(const QUrl& link) {
    QTcpSocket stream;
    Open(stream, link, Get(Changes));
    if (!QTest::qWaitFor([&] { return !Told(stream).isEmpty(); })) {
        qWarning("No picture was named.");
    }
    return Told(stream).value(0);
}

inline std::optional<QHostAddress> NetworkAddress() {
    const QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
    const auto address = std::ranges::find_if(addresses, [](const QHostAddress& address) {
        return address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback();
    });
    return address == addresses.end() ? std::nullopt : std::optional(*address);
}
