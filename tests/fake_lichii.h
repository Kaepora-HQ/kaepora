#pragma once

#include "game_values.h"
#include "tracker.h"

#include "bytes.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QtEndian>

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct FakeValue {
    std::string_view name;
    ValueType type;
    quint32 value;
    std::string_view text;
};

constexpr quint64 ProgramAt = 0x8027C000;

class FakeLichii {
public:
    explicit FakeLichii(std::vector<FakeValue> values, const std::vector<quint64>& programs = { ProgramAt })
        : values(std::move(values)) {
        server.moveToThread(&thread);
        thread.start();
        onItsThread([&] { QObject::connect(&server, &QTcpServer::newConnection, &server, [this] { take(); }); });
        start(programs);
    }

    ~FakeLichii() {
        quit();
        thread.quit();
        thread.wait();
    }

    // Changes the value and leaves its saved copy, so a read of the copy gives the old value.
    void set(std::string_view name, quint32 value) {
        change(name, [&](FakeValue& held) { held.value = value; });
    }

    void set(std::string_view name, std::string_view text) {
        change(name, [&](FakeValue& held) { held.text = text; });
    }

    // As the game starts, until its title (measured in a running game): main's pointer to the manager's slot is main's
    // offset of the slot, before the loader relocates it, and the slot then holds no manager.
    void starting(bool relocated) {
        onItsThread([&] {
            qToLittleEndian(relocated ? slotAt : quint64(0x4e842a8), at(mainAt + 0x4C7E180, 8));
            qToLittleEndian(quint64(0), at(slotAt, 8));
        });
    }

    void quit() {
        onItsThread([&] {
            server.close();
            qDeleteAll(server.findChildren<QTcpSocket*>());
        });
    }

    int connections() {
        int open = 0;
        onItsThread([&] {
            for (const QTcpSocket* socket : server.findChildren<QTcpSocket*>()) {
                open += socket->state() == QAbstractSocket::ConnectedState ? 1 : 0;
            }
        });
        return open;
    }

    // How many connections the fake has taken, and how many times it was asked for the method.
    int taken() {
        int count = 0;
        onItsThread([&] { count = connectionsTaken; });
        return count;
    }

    int asked(const QString& method) {
        int count = 0;
        onItsThread([&] { count = asks.value(method); });
        return count;
    }

    void start(const std::vector<quint64>& programs) {
        onItsThread([&] {
            layOut(programs);
            server.listen(QHostAddress::LocalHost, port);
            port = server.serverPort();
        });
    }

    quint16 port = 0;

private:
    using enum ValueType;

    struct Table {
        ValueType type;
        int slot;
        int stride;
        int offset;
    };
    static constexpr Table Tables[] = { { Bool, 0, 0x18, 0x16 },
                                        { Int, 2, 0x28, 0x1C },
                                        { Enum, 6, 0x38, 0x1C },
                                        { String64, 16, 0x108, 0x74 },
                                        { UInt, 20, 0x28, 0x1C } };
    static constexpr int ReadLimit = 64 * 1024;
    static constexpr int PairSize = 8;
    static constexpr int MapCapacity = 2 * ReadLimit / PairSize;

    quint64 mainAt = 0;
    quint64 slotAt = 0;
    QThread thread;
    QTcpServer server;
    std::vector<FakeValue> values;
    std::map<quint64, QByteArray> memory;
    quint64 next = 0x2107000000;
    std::map<std::string_view, quint64> addresses;
    int connectionsTaken = 0;
    QHash<QString, int> asks;

    static quint32 Hash(std::string_view name) {
        return name.starts_with("0x") ? std::stoul(std::string(name.substr(2)), nullptr, 16) : Murmur3(name);
    }

    static int Width(ValueType type) {
        return type == Bool ? 1 : type == String64 ? 64 : 4;
    }

    void onItsThread(const std::function<void()>& call) {
        QMetaObject::invokeMethod(&server, call, Qt::BlockingQueuedConnection);
    }

    void change(std::string_view name, const std::function<void(FakeValue&)>& edit) {
        onItsThread([&] {
            for (FakeValue& held : values) {
                if (held.name == name) {
                    edit(held);
                    write(at(addresses.at(name), Width(held.type)), held);
                }
            }
        });
    }

    quint64 place(const QByteArray& bytes, std::optional<quint64> address = {}) {
        const quint64 start = address.value_or(next);
        memory[start] = bytes;
        next = std::max(next, start + bytes.size() + 0x10000);
        return start;
    }

    char* at(quint64 address, qint64 size) {
        auto region = memory.upper_bound(address);
        if (region == memory.begin()) {
            return nullptr;
        }
        --region;
        const quint64 offset = address - region->first;
        return offset + size <= quint64(region->second.size()) ? region->second.data() + offset : nullptr;
    }

    static void write(char* bytes, const FakeValue& value) {
        if (value.type == Bool) {
            *bytes = char(value.value);
        } else if (value.type == String64) {
            *std::ranges::copy(value.text, bytes).out = 0;
        } else {
            qToLittleEndian(value.value, bytes);
        }
    }

    void layOut(const std::vector<quint64>& programs) {
        memory.clear();
        for (const quint64 program : programs) {
            place(QByteArray::fromHex("010000005c70c4039070c403"), program);
        }
        QByteArray manager(0x8 + 0x60 * 21, 0);
        for (const Table& table : Tables) {
            std::vector<const FakeValue*> held;
            for (const FakeValue& value : values) {
                if (value.type == table.type) {
                    held.push_back(&value);
                }
            }
            // Empty pairs hold an index past the count, as a hash map's do.
            QByteArray pairs(MapCapacity * PairSize, char(0xFF));
            QByteArray entries(table.stride * held.size(), 0);
            for (size_t index = 0; index < held.size(); ++index) {
                const int pair = (index + 1) * MapCapacity / (held.size() + 1);
                qToLittleEndian(Hash(held[index]->name), pairs.data() + pair * PairSize);
                qToLittleEndian(quint32(index), pairs.data() + pair * PairSize + 4);
                char* value = entries.data() + index * table.stride + table.offset;
                write(value, *held[index]);
                write(value + Width(table.type), *held[index]);
            }
            const quint64 entriesAt = place(entries);
            for (size_t index = 0; index < held.size(); ++index) {
                addresses[held[index]->name] = entriesAt + index * table.stride + table.offset;
            }
            char* head = manager.data() + 0x8 + 0x60 * table.slot;
            qToLittleEndian(quint32(held.size()), head + 0x8);
            qToLittleEndian(entriesAt, head + 0x10);
            qToLittleEndian(place(pairs), head + 0x18);
            qToLittleEndian(quint32(MapCapacity), head + 0x24);
        }
        QByteArray pointer(8, 0);
        qToLittleEndian(place(manager), pointer.data());
        const QByteArray slot = pointer;
        slotAt = place(slot);
        qToLittleEndian(slotAt, pointer.data());
        if (!programs.empty()) {
            mainAt = programs.front();
            place(pointer, mainAt + 0x4C7E180);
        }
    }

    void take() {
        while (QTcpSocket* socket = server.nextPendingConnection()) {
            ++connectionsTaken;
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                while (socket->canReadLine()) {
                    const QJsonObject request = QJsonDocument::fromJson(socket->readLine()).object();
                    ++asks[request["method"].toString()];
                    QJsonObject reply = answer(request["method"].toString(), request["params"].toObject());
                    reply["id"] = request["id"];
                    socket->write(QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n');
                }
            });
        }
    }

    static quint64 Address(const QJsonValue& value) {
        return value.toString().toULongLong(nullptr, 16);
    }

    static QString Hex(quint64 address) {
        return "0x" + QString::number(address, 16);
    }

    QJsonObject answer(const QString& method, const QJsonObject& params) {
        if (method == "read_memory") {
            const quint64 address = Address(params["address"]);
            const int size = params["size"].toInt();
            if (size < 1 || size > ReadLimit) {
                return { { "error", QString("size must be 1 to %1 bytes").arg(ReadLimit) } };
            }
            const char* bytes = at(address, size);
            if (bytes == nullptr) {
                return { { "error", "guest address " + Hex(address) + " is not mapped" } };
            }
            return { { "result", QJsonObject{ { "hex", QString(QByteArray(bytes, size).toHex()) } } } };
        }
        if (method == "search_memory") {
            const QByteArray pattern = QByteArray::fromHex(params["pattern"].toString().toLatin1());
            const quint64 start = Address(params["start"]);
            const quint64 end = Address(params["end"]);
            const int most = params["max_hits"].toInt();
            QJsonArray hits;
            for (const auto& [first, bytes] : memory) {
                for (qsizetype found = bytes.indexOf(pattern); found != -1; found = bytes.indexOf(pattern, found + 1)) {
                    if (first + found >= start && first + found < end && hits.size() < most) {
                        hits.append(Hex(first + found));
                    }
                }
            }
            // Lichii stops at the last hit asked for, and its search is then not complete.
            return { { "result", QJsonObject{ { "hits", hits }, { "complete", hits.size() < most } } } };
        }
        return { { "error", "unknown method \"" + method + '"' } };
    }
};

inline std::vector<FakeValue> TrackerGame(const Tracker& tracker, const std::map<std::string_view, quint32>& held) {
    std::vector<FakeValue> game;
    for (const SavedValue& value : tracker.values()) {
        const auto found = held.find(value.name);
        game.push_back({ value.name, value.type, found != held.end() ? found->second : 0,
                         value.name == Scene.name ? "KokiriForest_Child" : "" });
    }
    return game;
}
