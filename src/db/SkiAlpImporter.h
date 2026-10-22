#pragma once

#include "SkiProImporter.h"

// Reads a SkiAlp Pro 11 event archive (*.spro): a ZIP with two SQLite databases with the
// SkiPro tables, "File1" (parameters, categories, competitors) and "File2" (start lists, times).
class SkiAlpImporter {
public:
    static bool read(const QString& sproFile, SkiProImporter::Result& out, QString* error, const QString& logosDir = QString());
};
