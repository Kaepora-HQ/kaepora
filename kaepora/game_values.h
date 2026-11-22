#pragma once

#include <QJsonObject>
#include <QTcpSocket>

#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

constexpr quint16 DebugServerPort = 21198;

enum class ValueType { Bool, Int, Enum, String64 };

struct SavedValue {
    // Or, for a value the game names nowhere, 0x and its hash in eight hex digits.
    std::string name;
    ValueType type;
};

// what() is a sentence a player can read.
struct GameUnreadable : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Its calls block.
class GameValues {
public:
    explicit GameValues(quint16 port);

    // A Bool as 0 or 1, an Int as its 32 bits, an Enum as its choice's hash, and a String64 as its text's hash.
    std::vector<quint32> read(std::span<const SavedValue> values);

private:
    QJsonObject call(const QString& method, const QJsonObject& params);
    QByteArray readMemory(quint64 address, qsizetype size);
    quint64 indexOf(const SavedValue& value, const char* head);

    QTcpSocket socket;
    int lastId = 0;
    quint64 program;
    struct Indexes {
        quint64 manager;
        std::map<ValueType, std::unordered_map<quint32, quint32>> byHash;
    } indexes{};
};
