#include "tracker_page.h"

#include <QImage>
#include <QNetworkInterface>
#include <QNetworkProxy>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrl>

#include <algorithm>
#include <array>

constexpr char Changes[] = "/changes";

QByteArray Picture(const QByteArray& name) {
    return "/picture.png?" + name;
}

QByteArray Get(const QByteArray& path) {
    return "GET " + path + " HTTP/1.1\r\nHost: kaepora\r\nAccept: */*\r\nUser-Agent: Mozilla/5.0\r\n\r\n";
}

QImage Drawn(QColor color) {
    QImage picture(3, 2, QImage::Format_ARGB32_Premultiplied);
    picture.fill(Qt::transparent);
    color.setAlpha(128);
    picture.setPixelColor(1, 0, color);
    color.setAlpha(255);
    picture.setPixelColor(2, 1, color);
    return picture;
}

QUrl Here(const TrackerPage& page) {
    return QUrl(page.links().value(0));
}

void Open(QTcpSocket& socket, const QUrl& link, const QByteArray& request) {
    socket.connectToHost(link.host(), link.port());
    socket.write(request);
}

QByteArray Ask(const QUrl& link, const QByteArray& request) {
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

Answer Read(const QByteArray& bytes) {
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

QByteArrayList Told(QTcpSocket& stream) {
    static const QRegularExpression data("^data: ?(.*)$", QRegularExpression::MultilineOption);
    QByteArrayList names;
    for (const QRegularExpressionMatch& event :
         data.globalMatch(QString::fromUtf8(Read(stream.peek(stream.bytesAvailable())).body))) {
        names.append(event.captured(1).toUtf8());
    }
    return names;
}

QByteArray Current(const QUrl& link) {
    QTcpSocket stream;
    Open(stream, link, Get(Changes));
    if (!QTest::qWaitFor([&] { return !Told(stream).isEmpty(); })) {
        qWarning("No picture was named.");
    }
    return Told(stream).value(0);
}

class TrackerPageTest : public QObject {
    Q_OBJECT

private slots:
    void thePageNamesThePictureAndTheChangesByAddressesItAnswers() {
        TrackerPage page(0);
        page.show(Drawn(Qt::red));
        QVERIFY(page.listen(false));
        const Answer html = Read(Ask(Here(page), Get("/")));
        QCOMPARE(html.code, 200);
        QCOMPARE(html.type, "text/html; charset=utf-8");
        const QByteArray name = Current(Here(page));
        static const QRegularExpression quotedPath("\"(/[^\"]*)\"");
        QSet<QByteArray> types;
        for (const QRegularExpressionMatch& path : quotedPath.globalMatch(QString::fromUtf8(html.body))) {
            QTcpSocket socket;
            Open(socket, Here(page), Get(path.captured(1).toUtf8().replace("?", "?" + name)));
            QTRY_VERIFY(socket.peek(socket.bytesAvailable()).contains("\r\n\r\n"));
            const Answer answer = Read(socket.peek(socket.bytesAvailable()));
            QCOMPARE(answer.code, 200);
            types.insert(answer.type);
        }
        QCOMPARE(types, QSet<QByteArray>({ "image/png", "text/event-stream" }));
    }

    void thePictureIsThePngLastShownWithItsSeeThroughPixels() {
        TrackerPage page(0);
        const QImage shown = Drawn(Qt::blue);
        page.show(Drawn(Qt::red));
        page.show(shown);
        QVERIFY(page.listen(false));
        const Answer png = Read(Ask(Here(page), Get(Picture(Current(Here(page))))));
        QCOMPARE(png.code, 200);
        QCOMPARE(png.type, "image/png");
        const QImage served = QImage::fromData(png.body, "PNG").convertToFormat(QImage::Format_ARGB32);
        QCOMPARE(served, shown.convertToFormat(QImage::Format_ARGB32));
        QCOMPARE(qAlpha(served.pixel(0, 0)), 0);
        QCOMPARE(qAlpha(served.pixel(1, 0)), 128);
    }

    void aPictureIsAnsweredOnlyUnderItsOwnName() {
        TrackerPage page(0);
        page.show(Drawn(Qt::red));
        QVERIFY(page.listen(false));
        const QByteArray red = Current(Here(page));
        page.show(Drawn(Qt::blue));
        const QByteArray blue = Current(Here(page));
        QVERIFY(Ask(Here(page), Get(Picture(red))).isEmpty());
        QCOMPARE(Read(Ask(Here(page), Get(Picture(blue)))).code, 200);
        page.show(Drawn(Qt::red));
        QVERIFY(Ask(Here(page), Get(Picture(blue))).isEmpty());
        QCOMPARE(Read(Ask(Here(page), Get(Picture(red)))).code, 200);
    }

    void aListenerIsToldThePictureAtOnceAndAgainEachTimeAnotherIsShown() {
        TrackerPage page(0);
        page.show(Drawn(Qt::red));
        QVERIFY(page.listen(false));
        QTcpSocket stream;
        Open(stream, Here(page), Get(Changes));
        QTRY_COMPARE(Told(stream).size(), 1);
        page.show(Drawn(Qt::red));
        QTest::qWait(200);
        QCOMPARE(Told(stream).size(), 1);
        page.show(Drawn(Qt::blue));
        QTRY_COMPARE(Told(stream).size(), 2);
        QCOMPARE_NE(Told(stream)[1], Told(stream)[0]);
        QTcpSocket later;
        Open(later, Here(page), Get(Changes));
        QTRY_COMPARE(Told(later), QByteArrayList{ Told(stream)[1] });
    }

    void otherComputersReachThePageOnlyOnceTheNetworkIsAllowed() {
        const QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
        const auto address = std::ranges::find_if(addresses, [](const QHostAddress& address) {
            return address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback();
        });
        if (address == addresses.end()) {
            QSKIP("This computer has no network address.");
        }
        TrackerPage page(0);
        QVERIFY(page.listen(false));
        QUrl there = Here(page);
        there.setHost(address->toString());
        QVERIFY(!page.links().contains(there.toString()));
        QVERIFY(Ask(there, Get("/")).isEmpty());
        QVERIFY(page.listen(true));
        there.setPort(Here(page).port());
        QVERIFY(page.links().contains(there.toString()));
        QCOMPARE(Read(Ask(there, Get("/"))).code, 200);
    }

    void aProxyLeavesThePageListeningToTheNetwork() {
        QTcpServer closed;
        QVERIFY(closed.listen(QHostAddress::LocalHost));
        const quint16 port = closed.serverPort();
        closed.close();
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::Socks5Proxy, "127.0.0.1", port));
        const QScopeGuard noProxy([] { QNetworkProxy::setApplicationProxy({}); });
        TrackerPage page(0);
        QVERIFY(page.listen(true));
        QCOMPARE(Read(Ask(Here(page), Get("/"))).code, 200);
    }

    void junkLeavesThePageAnswering_data() {
        QTest::addColumn<QByteArray>("junk");
        QTest::addColumn<bool>("refused");
        QTest::newRow("a request that has not ended") << QByteArray("GET / HTTP/1.1\r\nHost: kaepora\r\n") << false;
        QTest::newRow("an over-long line")
            << "GET /" + QByteArray(4 * TrackerPage::RequestLimit, 'a') + " HTTP/1.1\r\n\r\n"
            << true;
        QTest::newRow("a method other than GET") << QByteArray("POST / HTTP/1.1\r\nContent-Length: 0\r\n\r\n") << true;
        QTest::newRow("an unknown path") << Get("/../kaepora/tracker_page.cpp") << true;
    }

    void junkLeavesThePageAnswering() {
        QFETCH(QByteArray, junk);
        QFETCH(bool, refused);
        TrackerPage page(0);
        QVERIFY(page.listen(false));
        QTcpSocket sender;
        Open(sender, Here(page), junk);
        QCOMPARE(Read(Ask(Here(page), Get("/"))).code, 200);
        if (refused) {
            QTRY_COMPARE(sender.state(), QAbstractSocket::UnconnectedState);
            QVERIFY(sender.readAll().isEmpty());
        }
    }

    void manyConnectionsLeaveThePageAnsweringAndItsListenersTold() {
        TrackerPage page(0);
        page.show(Drawn(Qt::red));
        QVERIFY(page.listen(false));
        QTcpSocket listener;
        Open(listener, Here(page), Get(Changes));
        QTRY_COMPARE(Told(listener).size(), 1);

        std::array<QTcpSocket, TrackerPage::ConnectionLimit + 1> idle;
        for (QTcpSocket& socket : idle) {
            socket.connectToHost(Here(page).host(), Here(page).port());
        }
        QTRY_COMPARE(idle.front().state(), QAbstractSocket::UnconnectedState);
        QCOMPARE(Read(Ask(Here(page), Get("/"))).code, 200);
        page.show(Drawn(Qt::blue));
        QTRY_COMPARE(Told(listener).size(), 2);

        std::array<QTcpSocket, TrackerPage::ConnectionLimit> more;
        for (QTcpSocket& stream : more) {
            Open(stream, Here(page), Get(Changes));
            QTRY_COMPARE(Told(stream).size(), 1);
        }
        QTRY_COMPARE(listener.state(), QAbstractSocket::UnconnectedState);
        QCOMPARE(Read(Ask(Here(page), Get("/"))).code, 200);
    }

    void aSecondPageOnTheSamePortCannotListen() {
        TrackerPage first(0);
        QVERIFY(first.listen(false));
        TrackerPage second(Here(first).port());
        QVERIFY(!second.listen(false));
        QVERIFY(second.links().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TrackerPageTest)
#include "tracker_page_test.moc"
