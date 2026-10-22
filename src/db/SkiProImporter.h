#pragma once

#include "core/Category.h"
#include "core/Competitor.h"
#include "core/EventParams.h"
#include "core/RunData.h"

#include <QString>
#include <QStringList>
#include <QVector>

class Event;

// Reads a SkiPro 2.x event (EventNNN.scdb + EventNNNEx.scdb, SQLite) and
// converts it to the OpenSkiPro2 model. SkiAlp Pro 11 uses the same tables (see SkiAlpImporter).
class SkiProImporter {
public:
    // the programs differ in the columns of some competitor fields
    enum class Format { SkiPro2, SkiAlp11 };

    struct Result {
        EventParams params;
        CategoryGrid categories;
        QVector<Competitor> competitors;
        QVector<RunData> runs;
        QStringList warnings;
    };

    // `mainFile` is the EventNNN.scdb file; the "Ex" file next to it is used when present.
    // `logosDir` is searched for logos whose path does not exist on this computer.
    static bool read(const QString& mainFile, Result& out, QString* error, const QString& logosDir = QString());
    // Same with the file of the runs given explicitly (empty: everything in `mainFile`).
    static bool readDatabases(const QString& mainFile, const QString& runsFile, Format format, Result& out, QString* error,
                              const QString& logosDir = QString());

    // Writes the result into an (already created/opened) event.
    static bool apply(const Result& data, Event& ev, QString* error);

    // Delphi TDateTime day number -> QDate
    static QDate fromDelphiDate(double days);
};
