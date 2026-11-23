#include "library.h"

#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QUuid>

#include <chrono>
#include <cstring>
#include <fstream>

nlohmann::json LogOf(const QString& folder) {
    return nlohmann::json::parse(std::ifstream(QFileInfo(folder + "/spoiler.json").filesystemFilePath()));
}

// As Qt 6.9's QUuid::createUuidV7 makes one, after RFC 9562, section 5.7: the milliseconds since 1970 in 48 bits, the
// version, 12 bits of the time within the millisecond, the variant and 62 random bits.
static QUuid NewVersion7() {
    using namespace std::chrono;
    const nanoseconds since = system_clock::now().time_since_epoch();
    const milliseconds whole = floor<milliseconds>(since);
    const quint64 ms = quint64(whole.count()) & 0xFFFF'FFFF'FFFF;
    QUuid id;
    id.data1 = uint(ms >> 16);
    id.data2 = ushort(ms);
    id.data3 = ushort(0x7000 | ((quint64((since - whole).count()) >> 8) & 0x0FFF));
    const quint64 random = QRandomGenerator::system()->generate64();
    std::memcpy(id.data4, &random, sizeof random);
    id.data4[0] = uchar((id.data4[0] & 0x3F) | 0x80);
    return id;
}

QUuid LibraryId(const QString& folder) {
    const QString path = folder + "/id.txt";
    QLockFile lock(path + ".lock");
    lock.lock();
    QFile kept(path);
    if (kept.open(QIODevice::ReadOnly)) {
        if (const QUuid id = QUuid::fromString(kept.readAll()); !id.isNull()) {
            return id;
        }
    }
    const QUuid id = NewVersion7();
    QSaveFile save(path);
    if (save.open(QIODevice::WriteOnly)) {
        save.write(id.toByteArray(QUuid::WithoutBraces));
        save.commit();
    }
    return id;
}
