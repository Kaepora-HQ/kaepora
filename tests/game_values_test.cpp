#include "fake_lichii.h"
#include "game_values.h"
#include "game_watch.h"

#include "bytes.h"

#include <QTcpServer>
#include <QTest>

#include <format>

using enum ValueType;

constexpr quint64 ElsewhereAt = 0x80E51000;

static QString Refusal(quint16 port) {
    try {
        GameValues game(port);
        return {};
    } catch (const GameUnreadable& unreadable) { return unreadable.what(); }
}

static QString Failure(GameValues& game, std::span<const SavedValue> values) {
    try {
        game.read(values);
        return {};
    } catch (const GameUnreadable& unreadable) { return unreadable.what(); }
}

class GameValuesTest : public QObject {
    Q_OBJECT

private slots:
    void readsAValueOfEachTypeByName() {
        FakeLichii lichii({ { "Opened", Bool, 0 },
                            { "Found", Bool, 1 },
                            { "Hearts", Int, 12 },
                            { "Sword", Enum, Murmur3("Kokiri") },
                            { "Grade", Enum, Murmur3("First") },
                            { .name = "Scene", .type = String64, .text = "KokiriForest_Child" } });
        lichii.set("Hearts", 13);
        lichii.set("Grade", Murmur3("Second"));
        lichii.set("Scene", "LostWood_Child");
        const std::vector<SavedValue> wanted = {
            { "Found", Bool }, { "Hearts", Int }, { "Grade", Enum }, { "Scene", String64 }
        };
        QCOMPARE(GameValues(lichii.port).read(wanted),
                 (std::vector<quint32>{ 1, 13, Murmur3("Second"), Murmur3("LostWood_Child") }));
    }

    void readsAValueTheGameNamesOnlyByItsHash() {
        FakeLichii lichii({ { "Menu", Enum, Murmur3("Title") } });
        const std::vector<SavedValue> wanted = { { std::format("0x{:08x}", Murmur3("Menu")), Enum } };
        QCOMPARE(GameValues(lichii.port).read(wanted), (std::vector<quint32>{ Murmur3("Title") }));
    }

    void saysTheGameIsStartingUntilItsDataIsThere() {
        FakeLichii lichii({ { "Found", Bool, 1 } });
        GameValues game(lichii.port);
        const std::vector<SavedValue> found = { { "Found", Bool } };
        lichii.starting(false);
        QCOMPARE(Failure(game, found), "The game is starting.");
        lichii.starting(true);
        QCOMPARE(Failure(game, found), "The game is starting.");
    }

    void refusesWhenNothingListens() {
        QTcpServer closed;
        QVERIFY(closed.listen(QHostAddress::LocalHost));
        const quint16 port = closed.serverPort();
        closed.close();
        QCOMPARE(Refusal(port), "Kaepora cannot reach the game. Start it with Play.");
    }

    void refusesWhenLichiiRunsAnotherProgram() {
        FakeLichii lichii({}, {});
        QCOMPARE(Refusal(lichii.port),
                 "Lichii runs another game, or another version of this one. Kaepora works only with version 2.0.0.");
    }

    void refusesWhenTheProgramIsFoundTwice() {
        FakeLichii lichii({}, { ProgramAt, ElsewhereAt });
        QCOMPARE(Refusal(lichii.port),
                 "Kaepora finds the game twice in Lichii's memory and cannot tell which one runs.");
    }

    void aWatchReportsTheFirstReadAndEachChange() {
        std::vector<std::vector<quint32>> changes;
        QObject reports;
        FakeLichii lichii({ { "Found", Bool, 0 }, { "Rupees", Int, 5 } });
        GameWatch watch(lichii.port, { { "Found", Bool }, { "Rupees", Int } });
        connect(&watch, &GameWatch::changed, &reports,
                [&](const std::vector<quint32>& read) { changes.push_back(read); });
        watch.start();
        QTRY_COMPARE(changes, (std::vector<std::vector<quint32>>{ { 0, 5 } }));
        QTest::qWait(4 * GameWatch::ReadPause);
        QCOMPARE(changes.size(), 1);
        lichii.set("Found", 1);
        QTRY_COMPARE(changes, (std::vector<std::vector<quint32>>{ { 0, 5 }, { 1, 5 } }));
    }

    void aWatchSaysWhyBeforeItsFirstRead() {
        QStringList reasons;
        QObject reports;
        QTcpServer closed;
        QVERIFY(closed.listen(QHostAddress::LocalHost));
        const quint16 port = closed.serverPort();
        closed.close();
        GameWatch watch(port, { { "Found", Bool } });
        connect(&watch, &GameWatch::unreadable, &reports, [&](const QString& why) { reasons << why; });
        watch.start();
        QTRY_VERIFY(!reasons.isEmpty());
        QCOMPARE(reasons.front(), "Kaepora cannot reach the game. Start it with Play.");
    }

    void aWatchSaysWhyItLostTheGameAndFindsItAgainAtAnotherAddress() {
        std::vector<std::vector<quint32>> changes;
        QStringList reasons;
        QObject reports;
        FakeLichii lichii({ { "Found", Bool, 1 } });
        GameWatch watch(lichii.port, { { "Found", Bool } });
        connect(&watch, &GameWatch::changed, &reports,
                [&](const std::vector<quint32>& read) { changes.push_back(read); });
        connect(&watch, &GameWatch::unreadable, &reports, [&](const QString& why) { reasons << why; });
        watch.start();
        QTRY_COMPARE(changes.size(), 1);
        QVERIFY(reasons.isEmpty());
        lichii.quit();
        QTRY_VERIFY(!reasons.isEmpty());
        QCOMPARE(reasons.front(), "The game stopped answering.");
        lichii.start({ ElsewhereAt });
        QTRY_COMPARE(changes, (std::vector<std::vector<quint32>>{ { 1 }, { 1 } }));
    }
};

QTEST_GUILESS_MAIN(GameValuesTest)

#include "game_values_test.moc"
