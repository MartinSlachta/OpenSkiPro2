#pragma once

#include "core/FisList.h"

#include <QString>

// Reading of a FIS points list as published by FIS (Alpine Data Booklet 12.2):
// archive ALFP<list><season>F.zip (or P.zip) or a folder with its extracted files.
// Used files: AL<list><season>hdr.csv (header / validity), cat.csv (min / max penalty),
// dis.csv (penalty constants), com.csv (competitors) and pts.csv (points).
namespace FisListReader {

bool read(const QString& path, FisList& list, QString* error);

} // namespace FisListReader

// The imported FIS list is kept in its own SQLite database, shared by all events.
// Only one list is stored: an import replaces the previous one.
namespace FisListDb {

bool save(const QString& dbPath, const FisList& list, QString* error);
// A missing database gives an empty list (and true).
bool load(const QString& dbPath, FisList& list, QString* error);
// Header and number of competitors only (fast, for the status).
bool loadHeader(const QString& dbPath, FisList& list, int* competitorCount, QString* error);

} // namespace FisListDb
