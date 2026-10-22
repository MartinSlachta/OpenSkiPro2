#pragma once

#include "core/Competitor.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

// Reading of tabular files (CSV / TXT / XLSX) and conversion to competitors
// using a column -> competitor field mapping.
namespace CsvImporter {

enum class Encoding { Auto, Utf8, System, Latin1 };

struct Table {
    QList<QStringList> rows;
    int columnCount() const;
};

// Reads .csv/.txt (delimiter auto-detected when `delimiter` is null) or .xlsx (first sheet).
bool readFile(const QString& path, Table& out, QString* error, QChar delimiter = QChar(), Encoding enc = Encoding::Auto);
bool parseText(const QString& text, Table& out, QChar delimiter = QChar());
QChar detectDelimiter(const QString& text);

// Guesses the competitor field of each column from a header row (0 = ignore).
QList<int> guessMapping(const QStringList& header);

// Builds competitors from rows [firstRow..]; mapping[i] = competitor field id of column i (0 = ignored).
QVector<Competitor> toCompetitors(const Table& table, const QList<int>& mapping, int firstRow);

} // namespace CsvImporter
