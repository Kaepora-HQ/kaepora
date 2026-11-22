#include "game_values.h"

#include "bytes.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QtEndian>

#include <algorithm>

// main's first 12 bytes in version 2.0.0, and where in Lichii's memory main loads.
constexpr char ProgramStart[] = "010000005c70c4039070c403";
constexpr quint64 ProgramFrom = 0x80000000;
constexpr quint64 ProgramTo = 0x82000000;

// [[main + ManagerGlobal]] is the game data manager. It holds one table per type, 0x60 bytes apart from manager + 0x8,
// each with its entry count at +0x8, its entries at +0x10, a map of (hash, index) pairs at +0x18 and the map's capacity
// at +0x24.
constexpr quint64 ManagerGlobal = 0x4C7E180;
constexpr quint64 TableSpacing = 0x60;
constexpr qsizetype TableHeadSize = 0x28;

struct Table {
    ValueType type;
    quint64 slot;
    quint64 stride;
    quint64 offset;
    qsizetype width;
};

constexpr Table Tables[] = { { ValueType::Bool, 0, 0x18, 0x16, 1 },
                             { ValueType::Int, 2, 0x28, 0x1C, 4 },
                             { ValueType::Enum, 6, 0x38, 0x1C, 4 },
                             { ValueType::String64, 16, 0x108, 0x74, 64 } };
constexpr qsizetype TablesSize = TableSpacing * std::ranges::max(Tables, {}, &Table::slot).slot + TableHeadSize;

// The largest table, Bool, holds 11,292 values, so no map of the game's has 2^20 pairs.
constexpr quint32 MostPairs = 1 << 20;

// The most bytes Lichii returns from one read.
constexpr qsizetype ReadLimit = 64 * 1024;

// How long a call waits for Lichii. The longest call, a search, ends after 2 seconds.
constexpr int Timeout = 5000;

constexpr char Garbled[] = "Lichii gave an answer Kaepora does not understand.";
constexpr char Unreached[] = "Kaepora cannot reach the game. Start it with Play.";

namespace {
// Lichii answered a call with an error, as it does for a read of memory the game has not mapped.
struct Refused : GameUnreadable {
    using GameUnreadable::GameUnreadable;
};
} // namespace

static quint32 Hash(const std::string& name) {
    return name.starts_with("0x") ? std::stoul(name.substr(2), nullptr, 16) : Murmur3(name);
}

static QString Hex(quint64 address) {
    return "0x" + QString::number(address, 16);
}

static quint64 Address(const QJsonValue& value) {
    bool ok = false;
    const quint64 address = value.toString().toULongLong(&ok, 16);
    if (!ok) {
        throw GameUnreadable(Garbled);
    }
    return address;
}

GameValues::GameValues(quint16 port) {
    socket.connectToHost(QHostAddress::LocalHost, port);
    if (!socket.waitForConnected(Timeout)) {
        throw GameUnreadable(Unreached);
    }
    const QJsonObject search = call("search_memory", { { "pattern", ProgramStart },
                                                       { "regions", "all" },
                                                       { "start", Hex(ProgramFrom) },
                                                       { "end", Hex(ProgramTo) },
                                                       { "max_hits", 2 } });
    const QJsonArray hits = search["hits"].toArray();
    if (hits.size() > 1) {
        throw GameUnreadable("Kaepora finds the game twice in Lichii's memory and cannot tell which one runs.");
    }
    if (!search["complete"].toBool()) {
        throw GameUnreadable("Lichii was too busy to look for the game.");
    }
    if (hits.isEmpty()) {
        throw GameUnreadable(
            "Lichii runs another game, or another version of this one. Kaepora works only with version 2.0.0.");
    }
    program = Address(hits[0]);
}

std::vector<quint32> GameValues::read(std::span<const SavedValue> values) {
    const auto pointer = [this](quint64 address) {
        return qFromLittleEndian<quint64>(readMemory(address, 8).constData());
    };
    // From the boot until the title, main's pointer to the manager leads nowhere Lichii can read (measured in a running
    // game): main's offset of the manager's slot until the loader relocates it, then a slot that holds no manager.
    quint64 manager = 0;
    QByteArray tables;
    try {
        manager = pointer(pointer(program + ManagerGlobal));
        tables = readMemory(manager + 0x8, TablesSize);
    } catch (const Refused&) { throw GameUnreadable("The game is starting."); }
    if (manager != indexes.manager) {
        indexes = { manager, {} };
    }
    std::vector<quint32> read;
    for (const SavedValue& value : values) {
        const Table& table = *std::ranges::find(Tables, value.type, &Table::type);
        const char* head = tables.constData() + TableSpacing * table.slot;
        const quint64 entry = qFromLittleEndian<quint64>(head + 0x10) + indexOf(value, head) * table.stride;
        const QByteArray held = readMemory(entry + table.offset, table.width);
        read.push_back(value.type == ValueType::Bool       ? held[0] != 0
                       : value.type == ValueType::String64 ? Murmur3(held.constData())
                                                           : qFromLittleEndian<quint32>(held.constData()));
    }
    return read;
}

QJsonObject GameValues::call(const QString& method, const QJsonObject& params) {
    const int id = ++lastId;
    socket.write(QJsonDocument(QJsonObject{ { "id", id }, { "method", method }, { "params", params } })
                     .toJson(QJsonDocument::Compact) +
                 '\n');
    while (!socket.canReadLine()) {
        if (!socket.waitForReadyRead(Timeout)) {
            throw GameUnreadable(id == 1 ? Unreached : "The game stopped answering.");
        }
    }
    const QJsonObject reply = QJsonDocument::fromJson(socket.readLine()).object();
    if (reply["id"].toInt() != id) {
        throw GameUnreadable(Garbled);
    }
    if (reply.contains("error")) {
        throw Refused("Lichii could not read the game: " + reply["error"].toString().toStdString());
    }
    return reply["result"].toObject();
}

QByteArray GameValues::readMemory(quint64 address, qsizetype size) {
    QByteArray bytes;
    for (qsizetype offset = 0; offset < size; offset += ReadLimit) {
        const qsizetype piece = std::min(ReadLimit, size - offset);
        const QJsonObject read =
            call("read_memory", { { "address", Hex(address + offset) }, { "size", qint64(piece) } });
        const QByteArray held = QByteArray::fromHex(read["hex"].toString().toLatin1());
        if (held.size() != piece) {
            throw GameUnreadable(Garbled);
        }
        bytes += held;
    }
    return bytes;
}

quint64 GameValues::indexOf(const SavedValue& value, const char* head) {
    std::unordered_map<quint32, quint32>& index = indexes.byHash[value.type];
    if (index.empty()) {
        const quint32 count = qFromLittleEndian<quint32>(head + 0x8);
        const quint32 capacity = qFromLittleEndian<quint32>(head + 0x24);
        if (capacity >= MostPairs) {
            throw GameUnreadable("The game's values are not where version 2.0.0 keeps them.");
        }
        const QByteArray pairs = readMemory(qFromLittleEndian<quint64>(head + 0x18), capacity * 8);
        for (quint32 pair = 0; pair < capacity; ++pair) {
            const quint32 at = qFromLittleEndian<quint32>(pairs.constData() + pair * 8 + 4);
            if (at < count) {
                index.emplace(qFromLittleEndian<quint32>(pairs.constData() + pair * 8), at);
            }
        }
    }
    const auto found = index.find(Hash(value.name));
    if (found == index.end()) {
        throw GameUnreadable("The game holds no value named " + value.name + ".");
    }
    return found->second;
}
