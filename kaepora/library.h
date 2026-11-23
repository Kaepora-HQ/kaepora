#pragma once

#include <nlohmann/json.hpp>

class QString;
class QUuid;

// The spoiler log of the seed in the library's folder. Throws when it cannot be read.
nlohmann::json LogOf(const QString& folder);

// The id of the seed in the library's folder: a version 7 UUID that names this copy of the seed and holds the time it
// was made. The first call makes it and keeps it in the folder, so every later call, from any Kaepora window, gives the
// same id. When the folder cannot be written, the id is not kept.
QUuid LibraryId(const QString& folder);
